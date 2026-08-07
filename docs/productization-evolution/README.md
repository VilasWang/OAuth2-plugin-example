# 产品化演进 · Productization Evolution

本目录承载 AuthForge 从开源项目向商业产品演进的规划与决策记录。

## 文档索引

| 文档 | 作用 | 日期 |
|------|------|------|
| [productization-research.md](productization-research.md) | 产品化调研报告（市场/竞品/路线选择）— 已于 2026-08-05 校准现状信息 | 2026-08-04（校准 08-05） |
| [productization-evolution-plan.md](productization-evolution-plan.md) | 产品化演进方案（路线图、优先级、风险） | 2026-08-05 |
| [benchmark-facility-design.md](benchmark-facility-design.md) | Phase 0 HTTP 性能基准设施技术设计（场景/策略/验收/milestone） | 2026-08-05 |
| [client-sdk-facility-design.md](client-sdk-facility-design.md) | Phase 1 多语言客户端 SDK 生成方案（OpenAPI 治理 + Python/Go 客户端） | 2026-08-05 |

## 与相关文档的关系

- **research**（调研报告）：市场/竞品/路线选择的**输入**。其性能数字（§3.1）目前是工程估算，待 benchmark 设施验证；其「API 稳定性」「多语言 SDK」卖点/差距由 client-sdk 设施填补。
- **evolution-plan**（演进方案）：对调研报告的**复盘 + 演进**，把"建立性能证据基线"提到最高优先级（Phase 0）。
- **benchmark-facility-design**（基准设计）：演进方案 Phase 0 的**落地蓝图**，是验证调研报告性能声明的唯一手段。
- **client-sdk-facility-design**（客户端 SDK 设计）：演进方案 Phase 1 的**落地蓝图**。前置一个 spec 治理地基（Layer 1），再生成 Python/Go 客户端（Layer 2）。与基准设计协同（附录 D）。
- **落地**：各设计文档中的具体工作项（benchmark milestone、spec 治理、客户端 milestone、企业协议实现等）应各自立项到 `openspec/changes/` 或 `.kiro/specs/`，本文档只做规划层锚点。
