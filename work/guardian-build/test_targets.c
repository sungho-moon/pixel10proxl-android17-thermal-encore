#include <assert.h>
#include <stdio.h>
#include "targets.h"
int main(void){
 FILE *f=fopen("dynamic-tids-test","w");assert(f);fputs("23411\n23412\n23413\n23414\n",f);fclose(f);
 assert(selected("MainThread",23411,10));
 assert(selected("UnnamedWorker",23412,10));
 assert(selected("RenderThread",23413,10));
 assert(selected("JobThread01",23414,10));
 assert(!selected("JobThread05",23415,10));
 assert(!selected("MainThread",10,10));
 remove("dynamic-tids-test");
 assert(allowed_policy(0)&&allowed_policy(3));
 assert(!allowed_policy(1)&&!allowed_policy(2)&&!allowed_policy(5)&&!allowed_policy(6));
 puts("TARGET_TEST_PASS dynamic_top4_only preserve_other_policies");return 0;
}
