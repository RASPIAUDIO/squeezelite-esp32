/* MnCast Sendspin settings on the existing configuration server. */
#include "http_server_handlers.h"
#include "cJSON.h"
#if CONFIG_MNCAST_SENDSPIN
#include "sendspin_sink.h"
#endif

esp_err_t sendspin_page_handler(httpd_req_t *req) {
    extern const char page[] asm("_binary_sendspin_html_start");
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Frame-Options","DENY");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'");
    return httpd_resp_sendstr(req,page);
}

static esp_err_t json_reply(httpd_req_t *req,const char *status,const char *body) {
    httpd_resp_set_status(req,status);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    return httpd_resp_sendstr(req,body);
}

esp_err_t sendspin_api_handler(httpd_req_t *req) {
#if !CONFIG_MNCAST_SENDSPIN
    return json_reply(req,"200 OK","{\"supported\":false}");
#else
    if(req->method==HTTP_GET) {
        char *json=mn_sendspin_status_json();
        if(!json) return json_reply(req,"503 Service Unavailable","{\"error\":\"Out of memory\"}");
        esp_err_t result=json_reply(req,"200 OK",json); free(json); return result;
    }
    /* No CORS grant. A custom header prevents cross-origin form submissions;
       Origin is checked too. No token in URLs, polling responses or logs. */
    char header[32],host[128],origin[144];
    if(httpd_req_get_hdr_value_str(req,"X-MnCast-Action",header,sizeof(header))!=ESP_OK || strcmp(header,"sendspin"))
        return json_reply(req,"403 Forbidden","{\"error\":\"Open the Sendspin page on this device\"}");
    size_t origin_len=httpd_req_get_hdr_value_len(req,"Origin");
    if(origin_len) {
        if(httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK ||
           httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK ||
           strncmp(origin,"http://",7) || strcmp(origin+7,host))
            return json_reply(req,"403 Forbidden","{\"error\":\"Origin rejected\"}");
    }
    if(req->content_len<2 || req->content_len>512)
        return json_reply(req,"400 Bad Request","{\"error\":\"Invalid request size\"}");
    char body[513]; int total=0;
    while(total<req->content_len) {
        int n=httpd_req_recv(req,body+total,req->content_len-total);
        if(n<=0) return ESP_FAIL;
        total+=n;
    }
    body[total]=0;
    cJSON *j=cJSON_Parse(body);
    cJSON *action=cJSON_GetObjectItemCaseSensitive(j,"action");
    if(!cJSON_IsString(action)) { cJSON_Delete(j); return json_reply(req,"400 Bad Request","{\"error\":\"Missing action\"}"); }
    if(!strcmp(action->valuestring,"token")) {
        cJSON_Delete(j);
        char *token=mn_sendspin_web_token();
        if(!token) return json_reply(req,"409 Conflict","{\"error\":\"Enable Sendspin and restart first\"}");
        cJSON *out=cJSON_CreateObject(); cJSON_AddStringToObject(out,"token",token);
        memset(token,0,strlen(token)); free(token);
        char *json=cJSON_PrintUnformatted(out); cJSON_Delete(out);
        if(!json) return ESP_ERR_NO_MEM;
        esp_err_t result=json_reply(req,"200 OK",json); memset(json,0,strlen(json)); free(json); return result;
    }
    bool save=!strcmp(action->valuestring,"save");
    bool forget=!strcmp(action->valuestring,"forget");
    cJSON *enabled=cJSON_GetObjectItemCaseSensitive(j,"enabled");
    cJSON *name=cJSON_GetObjectItemCaseSensitive(j,"name");
    cJSON *confirm=cJSON_GetObjectItemCaseSensitive(j,"confirm");
    if((!save && !forget) || !cJSON_IsBool(enabled) || !cJSON_IsString(name) ||
       (forget && (!cJSON_IsString(confirm) || strcmp(confirm->valuestring,"forget-pairings")))) {
        cJSON_Delete(j); return json_reply(req,"400 Bad Request","{\"error\":\"Invalid settings or confirmation\"}");
    }
    bool ok=mn_sendspin_web_config(cJSON_IsTrue(enabled),name->valuestring,forget);
    cJSON_Delete(j);
    return json_reply(req,ok ? "200 OK" : "400 Bad Request",ok ? "{\"saved\":true,\"restart_required\":true}" : "{\"error\":\"Name must contain 1-63 UTF-8 bytes without control characters; storage must be available\"}");
#endif
}
