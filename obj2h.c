#include <string.h>
#include <stdio.h>
#include <stdlib.h>

const int allocstep = 1000;

typedef struct
{
    float x;
    float y;
    float z;

    float u;
    float v;
} point;

typedef enum {false=0,true} bool;

int pointalloced = 0, pointusedxyz = 0, pointuseduv = 0;
point * points = NULL;

bool anyerror = false;

#define ALLOC(Type, Count) ((Type *)malloc(sizeof(Type) * (Count)))

int allocxyz(void)
{
    point * temp;
    int i;
    if(pointalloced <= pointusedxyz)
    {
        temp = ALLOC(point, pointalloced += allocstep);
        for(i=0;i<pointusedxyz||i<pointuseduv;i++)
        {
            temp[i] = points[i];
        }
        free(points);
        points = temp;
    }
    return pointusedxyz++;
}

int allocuv(void)
{
    point * temp;
    int i;
    if(pointalloced <= pointuseduv)
    {
        temp = ALLOC(point, pointalloced += allocstep);
        for(i=0;i<pointusedxyz||i<pointuseduv;i++)
        {
            temp[i] = points[i];
        }
        free(points);
        points = temp;
    }
    return pointuseduv++;
}

void convertfile(const char * filename)
{
    pointusedxyz = pointuseduv = 0;
    char infile[512], outfile[512];
    char * dotptr;
    FILE * is;
    FILE * os;
    int polycount = 0;
    char line[512];
    bool first = true;
    char pt[3][128];
    float u[3], v[3];
    int i;
    char * slash;
    int xyzindex, uvindex;
    int index;
    char temp[10];

    strcpy(infile, filename);
    dotptr = strrchr(infile, '.');
    if(dotptr)
    {
        *dotptr = '\0'; // temporarily remove extension
        strcpy(outfile, infile);
        *dotptr = '.'; // restore extension
        strcat(outfile, ".h");
    }
    else
    {
        strcpy(outfile, infile);
        strcat(infile, ".obj");
        strcat(outfile, ".h");
    }

    is = fopen(infile, "r");
    if(!is)
    {
        fprintf(stderr, "Error : Can't open \"%s\" for reading!\n", infile);
        anyerror = true;
        return;
    }
    os = fopen(outfile, "w");
    if(!os)
    {
        fprintf(stderr, "Error : Can't open \"%s\" for writing!\n", outfile);
        anyerror = true;
        return;
    }
    fprintf(os, "// Created by Obj2H by Jacob R. Lifshay\n"
                "// Converted from \"%s\".\n", infile);
    fprintf(os, "#ifndef SKIP_DEFINE_POLYGON\n"
                "struct polygon\n"
                "{\n"
                "    float x1, y1, z1;\n"
                "    float x2, y2, z2;\n"
                "    float x3, y3, z3;\n"
                "    float u1, v1;\n"
                "    float u2, v2;\n"
                "    float u3, v3;\n"
                "};\n"
                "#endif\n"
                "\n"
                "polygon polys[] =\n"
                "{\n");
    fflush(os);

    while(!ferror(is) && !feof(is))
    {
        fgets(line, 512, is);
        if(line[0] == '#') continue;
        if(line[0] == 'f')
        {
            if(!first)
            {
                fprintf(os, ",\n");
            }
            else
                first = false;
            fprintf(os, "    {");
            strtok(line, " ");
            strcpy(pt[0], strtok(NULL, " "));
            strcpy(pt[1], strtok(NULL, " "));
            strcpy(pt[2], strtok(NULL, " "));
            u[0] = 0;
            u[1] = 1;
            u[2] = 0;
            v[0] = 0;
            v[1] = 0;
            v[2] = 1;
            for(i=0;i<3;i++)
            {
                slash = strchr(pt[i], '/');
                if(slash)
                {
                    *slash++ = '\0';
                    sscanf(pt[i], "%i", &xyzindex);
                    sscanf(slash, "%i", &uvindex);
                    xyzindex--; uvindex--;
                    fprintf(os, "%g, %g, %g, ", points[xyzindex].x, points[xyzindex].y, points[xyzindex].z);
                    u[i] = points[uvindex].u;
                    v[i] = points[uvindex].v;
                }
                else
                {
                    sscanf(pt[i], "%i", &xyzindex);
                    xyzindex--;
                    fprintf(os, "%g, %g, %g, ", points[xyzindex].x, points[xyzindex].y, points[xyzindex].z);
                }
            }
            fprintf(os, "%g, %g, %g, %g, %g, %g}", u[0], v[0], u[1], v[1], u[2], v[2]);
            polycount++;
            fflush(os);
            continue;
        }
        else if(line[0] == 'v' && line[1] == ' ')
        {
            index = allocxyz();
            sscanf(line, "%s %f %f %f", temp, &points[index].x, &points[index].y, &points[index].z);
            continue;
        }
        else if(line[0] == 'v' && line[1] == 't')
        {
            index = allocuv();
            sscanf(line, "%s %f %f", temp, &points[index].u, &points[index].v);
            continue;
        }
    }
    fprintf(os, "\n"
                "};\n"
                "\n"
                "const int numpolys = %i;\n"
                "\n", polycount);
    fflush(os);
    fclose(os);
}

int main(int argc, char ** argv)
{
    int i;
    if(argc < 2)
    {
        printf("Usage : obj2h object1[.obj] [object2[.obj] [...]]\n");
        return 1;
    }
    for(i=1;i<argc;i++)
    {
        convertfile(argv[i]);
    }
    free(points);
    return anyerror;
}
