# TCP 帧格式

```
[4 byte BE uint32 length][protobuf Envelope bytes]
```

- length = Envelope 序列化字节数
- 最大 65536 bytes

参考：`beastserver/platform/net/tests/tcp_loopback_test.cpp` → `frame_bytes()`
