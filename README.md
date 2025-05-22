<div align=center>
<h1>telegram-database</h1>
<h3>快速、强大的 Telegram 群组/频道搜索机器人</h3>
</div>

#### 🇨🇳 本项目不向任何反华，赌博，诈骗群组提供支持！

## 简介
telegram-database（以下简称 tgdb）是一个第三方 Telegram 群组消息搜索机器人。由于 Telegram 官方搜索并没有进行中文分词，Telegram 的中文搜索体验极差。同时，类似的项目均需要在机器人聊天或网页上进行操作，使用不便，且不支持语义搜索等特性。此搜索机器人使用 Inline Query 进行搜索，便捷快速。

tgdb 基于 tdlib 使用 C++23 进行开发，对 tdlib 进行了 cpp20 coroutine 包装，比起基于 telegram-bot-api 的项目，可以实现主动索引消息，且部署便捷，延迟较低。

tgdb 支持基于 mllm embedding 的图文语义搜索，且进行了接口抽象，使得向量数据库实现和 embedding 服务替换方便，可拓展性强。目前，tgdb 支持基于阿里 GME 的图文多模态对齐语义搜索，未来可能还会支持 jina.ai clip-v2，BAAI BGE-VL 等模型。

tgdb 的主要服务是 self-contained 的，这意味着其可以直接运行，不用在外部开启任何服务，部署非常方便。

## 截图

![1000002147](https://github.com/user-attachments/assets/f6426898-4759-486e-959e-09a98da70d3b)
![1000002148](https://github.com/user-attachments/assets/58cce103-c6cc-4d83-b6ac-0a6b01bbaea6)
![1000002146](https://github.com/user-attachments/assets/cde51cd1-3836-4ab2-8a1c-e893197efa71)
