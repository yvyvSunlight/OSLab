#define _ORANGES_CMD_WHITELIST_H_

/*
 * 全局命令白名单：列出当前项目已实现并需要做校验/刷新校验和的“系统命令”。
 * 注意：这里的名字是根目录下的文件名（不带前导 '/'）。
 */
extern const char* g_syscmd_whitelist[];
extern const int   g_syscmd_whitelist_len;

// 判断 name 是否在白名单中（支持前导'/'，如 "/ls"）
int is_syscmd_whitelisted(const char* name);
