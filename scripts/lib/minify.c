/* See COPYING for copyright and license details */
#include <stdio.h>
#include <string.h>

/* Removes API comments */

int main(int argc, char **argv) {
    char line[1024];
    const char *cur;
    FILE *fin = NULL, *fout = NULL;
    int comment = 0;
    int ret = 0;

    if (argc < 3)
        return 1;

    fin = fopen(argv[1], "r");
    if (fin == NULL)
        return 1;

    fout = fopen(argv[2], "w");
    if (fout == NULL)
    {
        ret = 1;
        goto error_out;
    }
    
    while (fgets(line, sizeof line, fin))
    {
        if (comment)
        {
            if (strstr(line, "*/"))
                comment = 0;
            continue;
        }
        if ((cur = strstr(line, "/*")))
        {
            if (strstr(cur, "*/") == NULL)
                comment = 1;
            continue;
        }
        fprintf(fout, "%s", line);
    }

error_out:
    if (fin != NULL)
        fclose(fin);
    if (fout != NULL)
        fclose(fout);
    return ret;
}
