-- Migration: V025__audit_logs_partitioning
-- Created: 2026-08-18
-- Purpose: audit_logs 月度 RANGE 分区（已拍板方案 C）+ BRIN 辅助索引（方案 B）。
--   决策与依据：docs/productization-evolution/in-progress/noncode-performance-optimization.md §四.1
--   - 每笔 token 发放/刷新写一行审计，S2 写放大直接翻倍；分区后每分区索引小而浅，
--     INSERT 的 B-tree 维护成本不随总表增长上升；保留策略从 DELETE 变 DROP PARTITION。
--   - PK 必须包含分区键 → 改为 (id, timestamp) 复合主键。全仓库对 AuditLogs 仅
--     Mapper::insert（AuditLogger.cc）与 Criteria 查询（AuditService.cc），无
--     findByPrimaryKey / getPrimaryKey 调用 —— 零应用代码改动（配套 /orm-gen 再生成）。
--   - 4 个既有 B-tree（管理端查询实际使用）保留；新增 BRIN(timestamp) 作跨分区
--     时间扫描补充（KB 级体积，append 维护近零）。
--   - DEFAULT 分区兜底：窗口外时间戳（时钟偏移/忘记扩分区）不会导致 INSERT 失败。
--   - ensure_audit_partitions() 为运维扩展函数（零应用代码）：定期调用为未来月份
--     创建分区；DEFAULT 中落入已建分区范围的行会被安全迁出后再 ATTACH。
--   - UNLOGGED 方案已否决（crash 清空整表，破坏功能正确性）。
--
-- 幂等性：与仓库全部迁移一致，重复执行必须为 no-op —— psql 驱动的 db-reset 不写
-- schema_migrations，服务端启动（OAUTH2_AUTO_MIGRATE=true）会重放整链。顶层守卫：
-- audit_logs 已是分区表则整体跳过；对象级 IF NOT EXISTS 兜住部分应用的中间态。
-- 执行语义（SchemaManager：单事务、按顶层 ';' 拆分、$$ 感知）与 psql -f 均兼容。

-- === UP ===

DO $do$
DECLARE
    m DATE;
    m_end DATE;
    new_max_id BIGINT;
BEGIN
    IF EXISTS (
        SELECT 1 FROM pg_class c
        JOIN pg_partitioned_table p ON p.partrelid = c.oid
        WHERE c.relname = 'audit_logs' AND c.relnamespace = 'public'::regnamespace
    ) THEN
        RAISE NOTICE 'V025: audit_logs already partitioned, skipping';
    ELSE
        -- 1. 显式序列（不能用 BIGSERIAL：旧表序列 audit_logs_id_seq 在 DROP 前仍占用该名）
        IF to_regclass('public.audit_logs_part_id_seq') IS NULL THEN
            CREATE SEQUENCE audit_logs_part_id_seq;
        END IF;

        -- 2. 分区父表：结构与 V012 一致，PK 改为 (id, timestamp) 复合主键
        IF to_regclass('public.audit_logs_part') IS NULL THEN
            CREATE TABLE audit_logs_part (
                id BIGINT NOT NULL DEFAULT nextval('audit_logs_part_id_seq'),
                "timestamp" TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
                actor_type VARCHAR(20) NOT NULL,
                actor_id VARCHAR(128),
                action VARCHAR(50) NOT NULL,
                target_type VARCHAR(30),
                target_id VARCHAR(128),
                outcome VARCHAR(10) NOT NULL,
                ip VARCHAR(45),
                user_agent TEXT,
                request_id VARCHAR(64),
                details JSONB,
                PRIMARY KEY (id, "timestamp")
            ) PARTITION BY RANGE ("timestamp");
            ALTER SEQUENCE audit_logs_part_id_seq OWNED BY audit_logs_part.id;
        END IF;

        -- 3. 初始分区窗口：[当前月-12个月, 当前月+24个月)，UTC 月边界（跨环境一致）；
        --    分区名不依赖父表名（audit_logs_pYYYY_MM / audit_logs_default），父表改名后无需联动
        m := date_trunc('month', (now() AT TIME ZONE 'utc')::date) - INTERVAL '12 months';
        m_end := date_trunc('month', (now() AT TIME ZONE 'utc')::date) + INTERVAL '25 months';
        WHILE m < m_end LOOP
            IF to_regclass(format('public.audit_logs_p%s', to_char(m, 'YYYY_MM'))) IS NULL THEN
                EXECUTE format(
                    'CREATE TABLE audit_logs_p%s PARTITION OF audit_logs_part FOR VALUES FROM (%L) TO (%L)',
                    to_char(m, 'YYYY_MM'), m::timestamptz, (m + INTERVAL '1 month')::timestamptz);
            END IF;
            m := m + INTERVAL '1 month';
        END LOOP;
        -- 兜底分区最后建：窗口外时间戳（时钟偏移/扩分区空窗）落这里而不是 INSERT 失败
        IF to_regclass('public.audit_logs_default') IS NULL THEN
            CREATE TABLE audit_logs_default PARTITION OF audit_logs_part DEFAULT;
        END IF;

        -- 4. 存量数据迁移（仅当旧普通表还在；显式列清单；NULL timestamp 补当前时间）
        IF to_regclass('public.audit_logs') IS NOT NULL AND NOT EXISTS (
            SELECT 1 FROM pg_class c JOIN pg_partitioned_table p ON p.partrelid = c.oid
            WHERE c.relname = 'audit_logs' AND c.relnamespace = 'public'::regnamespace
        ) THEN
            INSERT INTO audit_logs_part
                (id, "timestamp", actor_type, actor_id, action, target_type, target_id,
                 outcome, ip, user_agent, request_id, details)
            SELECT id, COALESCE("timestamp", CURRENT_TIMESTAMP), actor_type, actor_id,
                   action, target_type, target_id, outcome, ip, user_agent, request_id, details
            FROM audit_logs;
            SELECT COALESCE(MAX(id), 0) INTO new_max_id FROM audit_logs;
            DROP TABLE audit_logs;
        ELSE
            SELECT COALESCE(MAX(id), 0) INTO new_max_id FROM audit_logs_part;
        END IF;

        -- 5. 换名：新表接管 audit_logs 名字（若旧表已在上一步删除）
        IF to_regclass('public.audit_logs') IS NULL THEN
            ALTER TABLE audit_logs_part RENAME TO audit_logs;
            ALTER SEQUENCE audit_logs_part_id_seq RENAME TO audit_logs_id_seq;
        END IF;

        -- 6. 序列对齐：INSERT ... SELECT 显式携带 id 不会推进序列，必须重置到 MAX(id)+1，
        --    否则下一条应用侧 INSERT（省略 id）会与已迁移的最大 id 主键冲突
        IF to_regclass('public.audit_logs_id_seq') IS NOT NULL THEN
            PERFORM setval('audit_logs_id_seq',
                           COALESCE((SELECT MAX(id) FROM audit_logs), 0) + 1, false);
        END IF;
        RAISE NOTICE 'V025: audit_logs partitioned (migrated max id = %)', new_max_id;
    END IF;
END
$do$;

-- 7. 索引：4 个既有 B-tree 原名重建（在分区父表上声明，自动传播到各分区），
--    新增 BRIN(timestamp) 补充索引。IF NOT EXISTS 兜中间态。
CREATE INDEX IF NOT EXISTS idx_audit_timestamp ON audit_logs("timestamp");
CREATE INDEX IF NOT EXISTS idx_audit_actor ON audit_logs(actor_type, actor_id);
CREATE INDEX IF NOT EXISTS idx_audit_action ON audit_logs(action);
CREATE INDEX IF NOT EXISTS idx_audit_outcome ON audit_logs(outcome, "timestamp");
CREATE INDEX IF NOT EXISTS idx_audit_timestamp_brin ON audit_logs USING BRIN ("timestamp");

-- 8. 运维扩展函数：为 [当前月-behind, 当前月+ahead] 创建缺失的月分区。
--    DEFAULT 中落入新分区范围的行先迁出到独立表，DELETE 后 ATTACH
--    （直接 UPDATE 分区键在无目标分区时会失败，故走 INSERT+DELETE+ATTACH）。
--    定期调用（cron / 运维手册）：SELECT ensure_audit_partitions();
CREATE OR REPLACE FUNCTION ensure_audit_partitions(ahead_months INTEGER DEFAULT 24, behind_months INTEGER DEFAULT 12)
RETURNS INTEGER AS $fn$
DECLARE
    m DATE;
    m_end DATE;
    lo TIMESTAMPTZ;
    hi TIMESTAMPTZ;
    part_name TEXT;
    created INTEGER := 0;
BEGIN
    m := date_trunc('month', (now() AT TIME ZONE 'utc')::date) - (behind_months || ' months')::INTERVAL;
    m_end := date_trunc('month', (now() AT TIME ZONE 'utc')::date) + ((ahead_months + 1) || ' months')::INTERVAL;
    WHILE m < m_end LOOP
        lo := m::timestamptz;
        hi := (m + INTERVAL '1 month')::timestamptz;
        part_name := 'audit_logs_p' || to_char(m, 'YYYY_MM');
        IF to_regclass(part_name) IS NULL THEN
            EXECUTE format('CREATE TABLE %I (LIKE audit_logs INCLUDING DEFAULTS)', part_name);
            IF to_regclass('audit_logs_default') IS NOT NULL THEN
                EXECUTE format(
                    'INSERT INTO %I SELECT * FROM audit_logs WHERE "timestamp" >= %L AND "timestamp" < %L',
                    part_name, lo, hi);
                EXECUTE format(
                    'DELETE FROM audit_logs WHERE "timestamp" >= %L AND "timestamp" < %L', lo, hi);
            END IF;
            EXECUTE format(
                'ALTER TABLE audit_logs ATTACH PARTITION %I FOR VALUES FROM (%L) TO (%L)',
                part_name, lo, hi);
            created := created + 1;
        END IF;
        m := m + INTERVAL '1 month';
    END LOOP;
    RETURN created;
END;
$fn$ LANGUAGE plpgsql;

COMMENT ON FUNCTION ensure_audit_partitions IS
    'Creates missing monthly audit_logs partitions in [now-behind_months, now+ahead_months] (UTC boundaries). '
    'Rows in the DEFAULT partition belonging to a new range are evacuated (INSERT+DELETE) before ATTACH. '
    'Call periodically (cron/ops runbook) so INSERTs never miss their partition.';

-- 9. 幂等校验调用：分区已存在时为 no-op（返回 0）
SELECT ensure_audit_partitions();

-- === DOWN ===
-- 回滚（重建 V012 形态的普通表；分区数据会聚拢回单表，体积大时耗时）：
-- CREATE TABLE audit_logs_plain (
--     id BIGSERIAL PRIMARY KEY,
--     "timestamp" TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
--     actor_type VARCHAR(20) NOT NULL, actor_id VARCHAR(128),
--     action VARCHAR(50) NOT NULL, target_type VARCHAR(30), target_id VARCHAR(128),
--     outcome VARCHAR(10) NOT NULL, ip VARCHAR(45), user_agent TEXT,
--     request_id VARCHAR(64), details JSONB
-- );
-- INSERT INTO audit_logs_plain (id, "timestamp", actor_type, actor_id, action,
--     target_type, target_id, outcome, ip, user_agent, request_id, details)
--     SELECT id, "timestamp", actor_type, actor_id, action, target_type, target_id,
--            outcome, ip, user_agent, request_id, details FROM audit_logs;
-- DROP FUNCTION IF EXISTS ensure_audit_partitions();
-- DROP TABLE audit_logs;
-- ALTER TABLE audit_logs_plain RENAME TO audit_logs;
-- CREATE INDEX idx_audit_timestamp ON audit_logs("timestamp");
-- CREATE INDEX idx_audit_actor ON audit_logs(actor_type, actor_id);
-- CREATE INDEX idx_audit_action ON audit_logs(action);
-- CREATE INDEX idx_audit_outcome ON audit_logs(outcome, "timestamp");
