class_name BeastConfig
extends Resource
## BeastClient 连接与默认参数

@export var host: String = "127.0.0.1"
@export var port: int = 8010
@export var connect_timeout_sec: float = 5.0
@export var default_token: String = "dev:42"
@export var client_version: String = "1.0.0"
@export var device_id: String = "godot-client"
