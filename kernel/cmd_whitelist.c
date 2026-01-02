#include "type.h"
#include "string.h"

#include "sys/cmd_whitelist.h"

const char* g_syscmd_whitelist[] = {
	"echo",
	"pwd",
	"ls",
	"kill",
	"touch",
	"edit",
	"rm",
	"ps",
	"clear",
	"cat",
	"ret2text",
	"ret2shellcode",
	"ret2lib",
	"pstackof",
    "inject"
};

const int g_syscmd_whitelist_len = sizeof(g_syscmd_whitelist) / sizeof(g_syscmd_whitelist[0]);

int is_syscmd_whitelisted(const char* name)
{
	if (!name)
		return 0;

	/* 支持 "/ls" 这种输入 */
	if (name[0] == '/')
		name++;

	int i;
	for (i = 0; i < g_syscmd_whitelist_len; i++) {
		if (strcmp(name, g_syscmd_whitelist[i]) == 0)
			return 1;
	}
	return 0;
}
