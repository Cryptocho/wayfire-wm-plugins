// T1 linkage check: obtheme.h must be includable and linkable from C++.
#include "obtheme.h"

int main()
{
    struct ob_theme_config cfg = {};
    (void)cfg;
    return 0;
}
