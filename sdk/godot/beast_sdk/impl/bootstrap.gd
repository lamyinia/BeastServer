class_name BeastBootstrap
extends RefCounted

static func create_transport():
	return load("res://beast_sdk/impl/transport/tcp_transport.gd").new()
