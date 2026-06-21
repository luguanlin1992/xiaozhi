#include "cc_notify_server.h"
#include "application.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include <cstdlib>
#include <string>

#define TAG "CcNotify"

CcNotifyServer::CcNotifyServer() : server_handle_(nullptr) {}

CcNotifyServer::~CcNotifyServer() {
    Stop();
}

// 把一条通知转成本地铃声 + 屏幕提示。level 决定提示音与表情。
void CcNotifyServer::ShowNotification(const char* level, const char* title, const char* text) {
    const char* status = (title != nullptr && title[0] != '\0') ? title : "Claude Code";
    const char* message = (text != nullptr) ? text : "";

    const char* emotion = "neutral";
    std::string_view sound = Lang::Sounds::OGG_POPUP;
    if (level != nullptr) {
        if (strcmp(level, "permission") == 0) {
            emotion = "thinking";
            sound = Lang::Sounds::OGG_EXCLAMATION;
        } else if (strcmp(level, "question") == 0) {
            emotion = "thinking";
            sound = Lang::Sounds::OGG_POPUP;
        } else if (strcmp(level, "done") == 0) {
            emotion = "happy";
            sound = Lang::Sounds::OGG_SUCCESS;
        }
    }

    ESP_LOGI(TAG, "notify [%s] %s: %s", level ? level : "", status, message);
    Application::GetInstance().Alert(status, message, emotion, sound);
}

esp_err_t CcNotifyServer::NotifyHandler(httpd_req_t* req) {
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }

    char* buf = static_cast<char*>(malloc(total + 1));
    if (buf == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            free(buf);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total] = '\0';

    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }

    cJSON* level = cJSON_GetObjectItem(root, "level");
    cJSON* title = cJSON_GetObjectItem(root, "title");
    cJSON* text = cJSON_GetObjectItem(root, "text");
    ShowNotification(cJSON_IsString(level) ? level->valuestring : nullptr,
                     cJSON_IsString(title) ? title->valuestring : nullptr,
                     cJSON_IsString(text) ? text->valuestring : nullptr);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

bool CcNotifyServer::Start(int port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.ctrl_port = port + 1;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;

    if (httpd_start(&server_handle_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start notify server");
        server_handle_ = nullptr;
        return false;
    }

    httpd_uri_t notify_uri = {};
    notify_uri.uri = "/notify";
    notify_uri.method = HTTP_POST;
    notify_uri.handler = NotifyHandler;
    notify_uri.user_ctx = nullptr;
    httpd_register_uri_handler(server_handle_, &notify_uri);
    ESP_LOGI(TAG, "Notify server started on port %d (POST /notify)", port);
    return true;
}

void CcNotifyServer::Stop() {
    if (server_handle_ != nullptr) {
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
        ESP_LOGI(TAG, "Notify server stopped");
    }
}
