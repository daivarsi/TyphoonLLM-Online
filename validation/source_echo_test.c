#include <assert.h>
#include <stdio.h>
#include "../vita/src/source_echo.h"
int main(void){
 assert(source_echo("What object is that?","What object is that?"));
 assert(source_echo(" what object is that. ","What object is that?"));
 assert(!source_echo("It is a telescope.","What object is that?"));
 assert(!source_echo("That object","What object is that?"));
 assert(!source_echo("",""));
 assert(!source_echo("Yes.","Answer yes or no."));
 puts("6 literal-input-echo checks passed");return 0;
}
