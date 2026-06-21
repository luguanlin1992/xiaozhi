#ifndef CC_NOTIFY_SERVER_H
#define CC_NOTIFY_SERVER_H

#include <esp_http_server.h>

// 本地通知服务：PC 上的 cc-bridge 通过 LAN 把 Claude Code 事件
// (需权限 / 提问 / 执行结束) POST 到设备，设备用本地铃声 + 屏幕提示用户。
// 这是单向 bridge -> device 推送；真正的批准/回答仍走云端语音对话。
//
// 接口：POST /notify  body: {"level":"permission|question|done","title":"..","text":".."}
class CcNotifyServer {
public:
    CcNotifyServer();
    ~CcNotifyServer();

    bool Start(int port = 8930);
    void Stop();

private:
    httpd_handle_t server_handle_;

    static esp_err_t NotifyHandler(httpd_req_t* req);
    static void ShowNotification(const char* level, const char* title, const char* text);
};

#endif // CC_NOTIFY_SERVER_H
