// SPDX-License-Identifier: GPL-3.0-or-later
#include "bili_feed.h"
#include "search_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>

extern void bili_feed_parse_item(struct json_object *obj, bili_feed_item_t *item);

int main(void)
{
    // 1. Test video item parser with search result containing <em> keyword tags
    const char *mock_search_item = "{\n"
        "  \"bvid\": \"BV1uv411q7Mv\",\n"
        "  \"title\": \"【原神】<em>测试</em>视频标题 720P\",\n"
        "  \"pic\": \"https://i0.hdslb.com/bfs/archive/test.jpg\",\n"
        "  \"author\": \"测试UP主\",\n"
        "  \"duration\": \"04:25\",\n"
        "  \"stat\": {\"view\": 123456}\n"
        "}";

    struct json_object *obj = json_tokener_parse(mock_search_item);
    assert(obj);

    bili_feed_item_t item;
    bili_feed_parse_item(obj, &item);

    assert(strcmp(item.bvid, "BV1uv411q7Mv") == 0);
    assert(strcmp(item.title, "【原神】测试视频标题 720P") == 0);
    assert(strcmp(item.author, "测试UP主") == 0);
    assert(item.duration_sec == 265);
    assert(item.view_count == 123456);

    json_object_put(obj);
    puts("PASS HTML tag stripping and item parsing test");

    bili_feed_state_t st;
    bili_feed_init(&st, NULL, NULL, NULL);
    assert(st.item_count == 0);
    bili_feed_shutdown(&st);
    st.current_tab=FEED_TAB_HISTORY;
    const char *history="{\"code\":0,\"data\":{\"cursor\":{\"max\":42,\"view_at\":1700000000},\"list\":[{\"history\":{\"bvid\":\"BV17x411w7KC\"},\"title\":\"历史测试\",\"author_name\":\"作者\",\"progress\":90,\"duration\":200}]}}";
    assert(!bili_feed_parse_response(&st,history,0));assert(st.item_count==1 && st.items[0].progress_sec==90);
    assert(!strcmp(st.items[0].author,"作者") && st.has_more && st.next_max==42);
    const char *search="{\"code\":0,\"data\":{\"result\":[{\"bvid\":\"BV17x411w7KC\",\"title\":\"<em>中文</em>搜索\",\"play\":2333}]}}";
    assert(!bili_feed_parse_response(&st,search,1));assert(st.item_count==1 && st.items[0].view_count==2333);
    assert(bili_feed_parse_response(&st,"{\"code\":-101}",0)<0);
    char encoded[128],utf8[32];uint16_t wide[32];
    assert(wili_url_encode("中文 &?",encoded,sizeof(encoded))>0);
    assert(!strcmp(encoded,"%E4%B8%AD%E6%96%87%20%26%3F"));
    assert(wili_utf8_to_utf16("中文😀",wide,32)>0);assert(wili_utf16_to_utf8(wide,32,utf8,32)>0);assert(!strcmp(utf8,"中文😀"));
    uint16_t broken[]={0xd800,0};assert(wili_utf16_to_utf8(broken,2,utf8,32)<0);
    assert(wili_utf8_to_utf16("\xf0\x9f",wide,32)<0);
    puts("PASS history cursor/progress, search schema, API errors, Chinese OSK conversion and URL encoding");

    st.current_tab=FEED_TAB_FAVORITE;st.favorite_id=0;st.page=1;
    const char *folders="{\"code\":0,\"data\":{\"list\":[{\"id\":123456789012,\"title\":\"收藏夹测试\",\"media_count\":9}]}}";
    assert(!bili_feed_parse_response(&st,folders,0));assert(st.item_count==1 && st.items[0].is_folder && st.items[0].resource_id==123456789012ULL);
    st.favorite_id=123456789012ULL;
    const char *resources="{\"code\":0,\"data\":{\"has_more\":false,\"medias\":[{\"bvid\":\"BV17x411w7KC\",\"title\":\"收藏的视频\",\"cover\":\"//example.invalid/cover.jpg\",\"upper\":{\"name\":\"UP主\"}}]}}";
    assert(!bili_feed_parse_response(&st,resources,0));assert(st.item_count==1 && !st.items[0].is_folder && !st.has_more);
    assert(!strcmp(st.items[0].pic_url,"https://example.invalid/cover.jpg"));
    puts("PASS real favorites folder/resource schemas and 64-bit media IDs");
    puts("PASS bili_feed tests");
    return 0;
}
