extends SceneTree

const _PingMessageTest := preload("res://demo/tests/ping_message_test.gd")


func _initialize() -> void:
	_PingMessageTest.run_all()
	print("Demo tests OK")
	quit(0)
