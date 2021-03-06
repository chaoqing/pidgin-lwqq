#include <prpl.h>
#include <ft.h>
#include <string.h>
#include "qq_types.h"
#include "type.h"
#include "msg.h"
#include "async.h"
#include "smemory.h"
#include "background.h"


static void file_trans_request_denied(PurpleXfer* xfer)
{
}
static int file_trans_on_progress(void* data,size_t now,size_t total)
{
    PurpleXfer* xfer = data;
    if(purple_xfer_is_canceled(xfer)||purple_xfer_is_completed(xfer)) {
        return 1;
    }
    purple_xfer_set_size(xfer,total);
    xfer->bytes_sent = now;
    xfer->bytes_remaining = total-now;
    purple_xfer_update_progress(xfer);
    return 0;
}
#if 0
static void file_trans_on_start(LwqqAsyncEvent* event,void* data)
{
    PurpleXfer* xfer = data;
    purple_xfer_start(xfer,lwqq_async_event_get_result(event),NULL,0);
}
#endif
static void file_trans_complete(LwqqAsyncEvent* event,void* data)
{
    PurpleXfer* xfer = data;
    purple_xfer_set_completed(xfer,1);
}
static void file_trans_init(PurpleXfer* xfer)
{
    void** data = xfer->data;
    LwqqClient* lc = data[0];
    LwqqMsgFileMessage* file = data[1];
    s_free(data);
    const char* filename = purple_xfer_get_local_filename(xfer);
    xfer->start_time = time(NULL);
    LwqqAsyncEvent* ev = lwqq_msg_accept_file(lc,file,filename);
    if(ev == NULL){ lwqq_puts("file trans error ");return ; }
    lwqq_async_event_set_progress(ev,file_trans_on_progress,xfer);
    lwqq_async_add_event_listener(ev,file_trans_complete,xfer);
}
static void file_trans_cancel(PurpleXfer* xfer)
{
}

void file_message(LwqqClient* lc,LwqqMsgFileMessage* file)
{
    qq_account* ac = lwqq_client_userdata(lc);
    if(file->mode == MODE_RECV) {
        PurpleAccount* account = ac->account;
        //for(i=0;i<file->file_count;i++){
        LwqqBuddy* buddy = lwqq_buddy_find_buddy_by_uin(ac->qq, file->from);
        if(buddy == NULL || buddy->qqnumber == NULL) return;
        PurpleXfer* xfer = purple_xfer_new(account,PURPLE_XFER_RECEIVE,buddy->qqnumber);
        purple_xfer_set_filename(xfer,file->recv.name);
        purple_xfer_set_init_fnc(xfer,file_trans_init);
        purple_xfer_set_request_denied_fnc(xfer,file_trans_request_denied);
        purple_xfer_set_cancel_recv_fnc(xfer,file_trans_cancel);
        LwqqMsgFileMessage* fmsg = s_malloc(sizeof(*fmsg));
        memcpy(fmsg,file,sizeof(*fmsg));
        file->from = file->to = file->reply_ip = file->recv.name = NULL;
        void** data = s_malloc(sizeof(void*)*2);
        data[0] = lc;
        data[1] = fmsg;
        xfer->data = data;
        purple_xfer_request(xfer);
    } else if(file->mode == MODE_REFUSE) {
        if(file->refuse.cancel_type == CANCEL_BY_USER) {
            qq_sys_msg_write(ac, LWQQ_MT_BUDDY_MSG, file->from, "对方取消文件传输", PURPLE_MESSAGE_SYSTEM, time(NULL));
        } else if(file->refuse.cancel_type == CANCEL_BY_OVERTIME) {
            qq_sys_msg_write(ac, LWQQ_MT_BUDDY_MSG, file->from, "文件传输超时", PURPLE_MESSAGE_SYSTEM, time(NULL));
        }

    }

}

static void send_offline_file_receipt(LwqqAsyncEvent* ev,void* d)
{
    int errno = lwqq_async_event_get_result(ev);
    void **data = d;
    qq_account* ac = data[0];
    LwqqMsgOffFile* file = data[1];
    char* name = data[3];
    s_free(d);
    if(errno == 0){
        qq_sys_msg_write(ac, LWQQ_MT_BUDDY_MSG, name, "发送离线文件成功", PURPLE_MESSAGE_SYSTEM, time(NULL));
    }else{
        qq_sys_msg_write(ac, LWQQ_MT_BUDDY_MSG, name, "发送离线文件失败", PURPLE_MESSAGE_ERROR, time(NULL));
    }
    s_free(name);
    lwqq_msg_offfile_free(file);
}

static void send_file(LwqqAsyncEvent* event,void* d)
{
    if(d==NULL) return;
    void** data = d;
    qq_account* ac = data[0];
    LwqqClient* lc = ac->qq;
    long errno = 0;
#ifdef USE_LIBEV
    if(event){
        errno = lwqq_async_event_get_result(event);
        data[4] = (void*)errno;
        lc->dispatch(NULL,(DISPATCH_FUNC)send_file,d);
        return;
    }else{
        errno = (long)data[4];
    }
#else
    errno = lwqq_async_event_get_result(event);
#endif
    LwqqMsgOffFile* file = data[1];
    PurpleXfer* xfer = data[2];
    char* name = data[3];
    purple_xfer_set_completed(xfer,1);
    purple_xfer_unref(xfer);
    if(errno) {
        qq_sys_msg_write(ac,LWQQ_MT_BUDDY_MSG, name,"上传空间不足",PURPLE_MESSAGE_ERROR,time(NULL));
        lwqq_msg_offfile_free(file);
        s_free(name);
        s_free(d);
    } else {
        lwqq_async_add_event_listener(lwqq_msg_send_offfile(lc,file),send_offline_file_receipt,d);
    }
}
static void upload_offline_file_init(PurpleXfer* xfer)
{
    void** data = xfer->data;
    qq_account* ac = data[0];
    LwqqClient* lc = ac->qq;
    LwqqMsgOffFile* file = lwqq_msg_fill_upload_offline_file(
            xfer->local_filename, lc->myself->uin, purple_xfer_get_remote_user(xfer));
    xfer->start_time = time(NULL);
    data[1] = file;
    data[2] = xfer;
    LwqqAsyncEvent* ev = lwqq_msg_upload_offline_file(lc,file);
    lwqq_async_add_event_listener(ev,send_file,data);
    lwqq_async_event_set_progress(ev, file_trans_on_progress, xfer);
}
static void upload_file_init(PurpleXfer* xfer)
{
    void** data = xfer->data;
    qq_account* ac = data[0];
    LwqqClient* lc = ac->qq;
    LwqqMsgOffFile* file = s_malloc0(sizeof(*file));
    file->from = s_strdup(lc->myself->uin);
    file->to = s_strdup(purple_xfer_get_remote_user(xfer));
    file->name = s_strdup(purple_xfer_get_local_filename(xfer));
    xfer->start_time = time(NULL);
    data[1] = file;
    data[2] = xfer;
    //lwqq_async_add_event_listener(
    background_upload_file(lc,file,file_trans_on_progress,xfer);
    //send_file,data);
}

void qq_send_file(PurpleConnection* gc,const char* who,const char* filename)
{
    qq_account* ac = purple_connection_get_protocol_data(gc);
    if(!ac->debug_file_send){
        purple_notify_warning(gc,NULL,"难题尚未攻破,曙光遥遥无期","请先用离线文件传输");
        return;
    }
    PurpleAccount* account = ac->account;
    PurpleXfer* xfer = purple_xfer_new(account,PURPLE_XFER_SEND,who);
    purple_xfer_set_init_fnc(xfer,upload_file_init);
    //purple_xfer_set_init_fnc(xfer,upload_offline_file_init);
    purple_xfer_set_request_denied_fnc(xfer,file_trans_request_denied);
    purple_xfer_set_cancel_send_fnc(xfer,file_trans_cancel);
    void** data = s_malloc(sizeof(void*)*3);
    data[0] = ac;
    xfer->data = data;
    purple_xfer_request(xfer);
}

void qq_send_offline_file(PurpleBlistNode* node)
{
    PurpleBuddy* buddy = PURPLE_BUDDY(node);
    PurpleAccount* account = purple_buddy_get_account(buddy);
    qq_account* ac = purple_connection_get_protocol_data(
                         purple_account_get_connection(account));
    const char* who;
    if(ac->qq_use_qqnum){
        const char* qqnum = purple_buddy_get_name(buddy);
        LwqqBuddy* b = find_buddy_by_qqnumber(ac->qq, qqnum);
        if(b == NULL) return;
        who = b->uin;
    }else{
        who = purple_buddy_get_name(buddy);
    }
    PurpleXfer* xfer = purple_xfer_new(account,PURPLE_XFER_SEND,who);
    purple_xfer_set_init_fnc(xfer,upload_offline_file_init);
    purple_xfer_set_request_denied_fnc(xfer,file_trans_request_denied);
    purple_xfer_set_cancel_send_fnc(xfer,file_trans_cancel);
    void** data = s_malloc(sizeof(void*)*5);
    data[0] = ac;
    data[3] = strdup(purple_buddy_get_name(buddy));
    xfer->data = data;
    purple_xfer_request(xfer);
}
