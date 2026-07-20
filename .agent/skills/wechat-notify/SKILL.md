---
name: wechat-notify
description: 通过微信机器人发送通知消息（文本、图片、文件、视频）
allowed-tools: Bash(curl *)
---

通过本地 Bot API 向用户发送微信通知。

## 获取端口

端口配置在 `~/.cc-go/config.json` 的 `web_port` 字段中，发送请求前先读取：

```bash
PORT=$(grep -o '"web_port":[[:space:]]*[0-9]*' ~/.cc-go/config.json | grep -o '[0-9]*')
```

## 发送文本消息

```bash
curl -s -X POST http://localhost:$PORT/api/v1/wechat-bot/send/text \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"text":"通知内容"}'
```

## 发送图片

```bash
curl -s -X POST http://localhost:$PORT/api/v1/wechat-bot/send/image \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"file_path":"C:/path/to/image.png"}'
```

## 发送文件

```bash
curl -s -X POST http://localhost:$PORT/api/v1/wechat-bot/send/file \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"file_path":"C:/path/to/document.pdf"}'
```

## 发送视频

```bash
curl -s -X POST http://localhost:$PORT/api/v1/wechat-bot/send/video \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"file_path":"C:/path/to/video.mp4"}'
```

## 返回值

- `{"status":"sent"}` — 已发送
- `{"status":"buffered"}` — 已排队，等待预算重置后发送

## 适用场景

- 长时间任务完成通知
- 错误或重要状态变更提醒
- 主动推送结果或摘要
