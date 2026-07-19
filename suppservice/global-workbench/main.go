package main

import (
	"embed"
	"log"
	"os"
	"path/filepath"

	"github.com/wailsapp/wails/v3/pkg/application"

	"global-workbench/internal/logger"
)

// Wails 用 Go 的 embed 包把前端 dist 嵌入二进制。
// frontend/dist 目录的所有文件会嵌入并暴露给前端。
//
// (dev mode 下 exe 加载 vite dev server @ 127.0.0.1:9245，不读 dist；prod 才读 embed)
//
//go:embed all:frontend/dist
var assets embed.FS

func main() {
	app := application.New(application.Options{
		Name:        "global-workbench",
		Description: "BeastServer global workbench",
		Services: []application.Service{
			application.NewService(NewServerctlService()),
			application.NewService(NewRoomctlService()),
			application.NewService(NewSdkEventService()),
		},
		Assets: application.AssetOptions{
			Handler: application.AssetFileServerFS(assets),
		},
		// 独立 user-data-dir，避免 wails3 dev 模式下跟系统 WebView2 应用
		// （SearchHost/Widgets）共享资源导致 800700aa ERROR_BUSY。
		Windows: application.WindowsOptions{
			WebviewUserDataPath: filepath.Join(os.TempDir(), "global-workbench-webview2"),
		},
		Mac: application.MacOptions{
			ApplicationShouldTerminateAfterLastWindowClosed: true,
		},
	})

	app.Window.NewWithOptions(application.WebviewWindowOptions{
		Title:            "global-workbench",
		Width:            1024,
		Height:           768,
		BackgroundColour: application.NewRGB(27, 38, 54),
		URL:              "/",
	})

	// forwardLogsToFrontend 把后端 logger bus 的日志条目通过 Wails event 推给前端。
	// 前端通过 Events.On("log:entry", cb) 订阅，按 tag 分流到 LogWindow 对应 tab。
	// 启动一条 goroutine 消费 logger.Subscribe() channel 即可；进程退出时 channel
	// 不会关闭（defaultBus 生命周期 == 进程），但 app.Run() 退出后进程也终止，
	// goroutine 自然随之结束，无泄漏问题。
	go func() {
		ch := logger.Subscribe()
		for entry := range ch {
			app.Event.Emit("log:entry", entry)
		}
	}()

	if err := app.Run(); err != nil {
		log.Fatal(err)
	}
}
