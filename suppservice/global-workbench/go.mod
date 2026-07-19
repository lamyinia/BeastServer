module global-workbench

go 1.25.0

require (
	beastserver-project/sdk/go/beastcli v0.0.0
	github.com/wailsapp/wails/v3 v3.0.0-alpha2.117
	golang.org/x/crypto v0.51.0
	golang.org/x/term v0.43.0
	google.golang.org/grpc v1.82.1
	google.golang.org/protobuf v1.36.11
)

// beastcli 是唯一 Go SDK module（包含 proto/internal codec/internal transport/internal log/BeastClient）。
// 工作台通过 replace 引用本地路径，beastcli 改动即时生效。
replace beastserver-project/sdk/go/beastcli => ../../sdk/go/beastcli

require (
	github.com/adrg/xdg v0.5.3 // indirect
	github.com/coder/websocket v1.8.14 // indirect
	github.com/go-ole/go-ole v1.3.0 // indirect
	github.com/godbus/dbus/v5 v5.2.2 // indirect
	github.com/jchv/go-winloader v0.0.0-20250406163304-c1995be93bd1 // indirect
	github.com/mattn/go-colorable v0.1.14 // indirect
	github.com/mattn/go-isatty v0.0.20 // indirect
	golang.org/x/net v0.53.0 // indirect
	golang.org/x/sys v0.44.0 // indirect
	golang.org/x/text v0.37.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20260414002931-afd174a4e478 // indirect
)
