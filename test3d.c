#include <go32.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <dos.h>
#include <math.h>
#include <time.h>
#include <dpmi.h>
#include <string.h>
#include <pc.h>
#include <sys/movedata.h>

#define GENERATE_OBJECTS

typedef int Color;
#define RGB(r, g, b) ((Color)((((unsigned)(r) & 0xFF) << 16) | (((unsigned)(g) & 0xFF) << 8) | (((unsigned)(b) & 0xFF))))
#define GetRValue(c) ((unsigned)((Color)(c) & 0xFF0000) >> 16)
#define GetGValue(c) ((unsigned)((Color)(c) & 0xFF00) >> 8)
#define GetBValue(c) ((unsigned)((Color)(c) & 0xFF))

#define OVERLAY_TRANSPARENT 0xFFFFFFFF
#define PACKED __attribute__((__packed__))
//#define PACKED

struct Image
{
    Color * data;
    int XRes, YRes;
};

void freeImage(struct Image * img)
{
    if(!img) return;
    free(img->data);
    free(img);
}

struct PACKED BitmapCoreHeader
{
    long Size;
    short Width;
    short Height;
    short Planes;
    short BitCount;
};

#define BI_RGB 0L
#define BI_RLE8 1L
#define BI_RLE4 2L
#define BI_BITFIELDS 3L

struct PACKED BitmapInfoHeader
{
    long Size;
    long Width;
    long Height;
    short Planes;
    short BitCount;

    long Compression;
    long SizeImage;
    long XPelsPerMeter;
    long YPelsPerMeter;
    long ClrUsed;
    long ClrImportant;
};

const short bfTypeValue = (short)'B' + ((short)'M' << 8);

struct PACKED BitmapFileHeader
{
    short Type;
    long Size;
    short Reserved1;
    short Reserved2;
    long OffBits;
};

struct PACKED RGBTri
{
    unsigned char B;
    unsigned char G;
    unsigned char R;
};

struct PACKED RGBQuad
{
    unsigned char B;
    unsigned char G;
    unsigned char R;
    unsigned char Res;
};

/*static unsigned char win4bitconv[] =
{
    0x0, 0x4,
    0x2, 0x6,
    0x1, 0x5,
    0x3, 0x8,
    0x7, 0xC,
    0xA, 0xE,
    0x9, 0xD,
    0xB, 0xF
};*/

struct Image * loadBMPImage(const char * filename)
{
    FILE * is;
    struct BitmapFileHeader bfh;
    struct BitmapInfoHeader bih;
    struct BitmapCoreHeader bch;
    int iscore, bottomup;
    struct Image * retval;
    int bytelen;
    unsigned char * mem_;
    Color FC, BC;
    struct RGBQuad clr;
    int X, Y, i;
    unsigned char * mem;
    int palcount;
    Color palette[256];
    unsigned char b, c, cnt, x, y;
    Color clr1, clr2;
    struct RGBTri * tmem;
    struct RGBQuad * qmem;

    is = fopen(filename, "rb");
    if(!is || ferror(is)) {fclose(is); return NULL;}
    if(1 != fread((void *)&bfh, sizeof(bfh), 1, is)) {fclose(is); return NULL;}

    if(bfh.Type != bfTypeValue) {return NULL; fclose(is);}

    if(1 != fread((void *)&bih.Size, sizeof(bih.Size), 1, is)) {fclose(is); return NULL;}

    if(bih.Size != sizeof(struct BitmapInfoHeader) && bih.Size != sizeof(struct BitmapCoreHeader)) {fclose(is); return NULL;}

    bih.Compression = BI_RGB;

    iscore = (bih.Size == sizeof(struct BitmapCoreHeader));

    if(!iscore)
    {
        if(1 != fread((void *)&bih.Width, sizeof(bih.Width), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.Height, sizeof(bih.Height), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.Planes, sizeof(bih.Planes), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.BitCount, sizeof(bih.BitCount), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.Compression, sizeof(bih.Compression), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.SizeImage, sizeof(bih.SizeImage), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.XPelsPerMeter, sizeof(bih.XPelsPerMeter), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.YPelsPerMeter, sizeof(bih.YPelsPerMeter), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.ClrUsed, sizeof(bih.ClrUsed), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bih.ClrImportant, sizeof(bih.ClrImportant), 1, is)) {fclose(is); return NULL;}
    }
    else
    {
        if(1 != fread((void *)&bch.Width, sizeof(bch.Width), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bch.Height, sizeof(bch.Height), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bch.Planes, sizeof(bch.Planes), 1, is)) {fclose(is); return NULL;}
        if(1 != fread((void *)&bch.BitCount, sizeof(bch.BitCount), 1, is)) {fclose(is); return NULL;}
        bih.Width = bch.Width;
        bih.Height = bch.Height;
        bih.Planes = bch.Planes;
        bih.BitCount = bch.BitCount;
    }

    if(bih.Compression > BI_BITFIELDS || bih.Height == 0 || bih.Width <= 0 || bih.BitCount == 0) {fclose(is); return NULL;}

    bottomup = bih.Height > 0;
    if(!bottomup) bih.Height = -bih.Height; // ABS

    retval = (struct Image *)malloc(sizeof(struct Image));
    if(!retval) {fclose(is); return NULL;}
    retval->data = (Color *)malloc(sizeof(Color) * (int)bih.Width * (int)bih.Height);
    if(!retval->data) {fclose(is); free(retval); return NULL;}
    retval->XRes = (int)bih.Width;
    retval->YRes = (int)bih.Height;
    bytelen = (int)(((long)bih.Width * bih.BitCount + 31L) >> 3) & ~3;
    mem_ = (unsigned char *)malloc(sizeof(unsigned char) * bytelen);
    if(!mem_) {fclose(is); freeImage(retval); return NULL;}

    switch(bih.BitCount)
    {
      case 1:
      {
        if(bih.Compression != BI_RGB) {fclose(is); free(mem_); freeImage(retval); return NULL;}
        if(iscore)
        {
            if(1 != fread((void *)&clr.R, sizeof(clr.R), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.G, sizeof(clr.G), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.B, sizeof(clr.B), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            BC = RGB(clr.R, clr.G, clr.B);
            if(1 != fread((void *)&clr.R, sizeof(clr.R), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.G, sizeof(clr.G), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.B, sizeof(clr.B), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            FC = RGB(clr.R, clr.G, clr.B);
        }
        else
        {
            if(1 != fread((void *)&clr.R, sizeof(clr.R), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.G, sizeof(clr.G), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.B, sizeof(clr.B), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.Res, sizeof(clr.Res), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            BC = RGB(clr.R, clr.G, clr.B);
            if(1 != fread((void *)&clr.R, sizeof(clr.R), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.G, sizeof(clr.G), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.B, sizeof(clr.B), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            if(1 != fread((void *)&clr.Res, sizeof(clr.Res), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            FC = RGB(clr.R, clr.G, clr.B);
        }

        for(Y=(int)(bottomup?bih.Height-1:0);(bottomup?Y>=0:Y<bih.Height);(bottomup?Y--:Y++))
        {
            if(1 != fread((void *)mem_, bytelen, 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            mem = mem_;
            for(X=0;X<bih.Width;X++)
            {
                retval->data[retval->XRes * Y + X] = (((int)mem[X >> 3] & (0x80 >> (X & 7))) ? FC : BC);
            }
        }
        break;
      }
      case 4:
      {
        palcount = 16;
        if(iscore)
        {
            for(i=0;i<palcount;i++)
            {
                if(1 != fread((void *)&clr.R, sizeof(clr.R), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                if(1 != fread((void *)&clr.G, sizeof(clr.G), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                if(1 != fread((void *)&clr.B, sizeof(clr.B), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                palette[i] = RGB(clr.R, clr.G, clr.B);
            }
        }
        else
        {
            for(i=0;i<palcount;i++)
            {
                if(1 != fread((void *)&clr.R, sizeof(clr.R), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                if(1 != fread((void *)&clr.G, sizeof(clr.G), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                if(1 != fread((void *)&clr.B, sizeof(clr.B), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                if(1 != fread((void *)&clr.Res, sizeof(clr.Res), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                palette[i] = RGB(clr.R, clr.G, clr.B);
            }
        }

        /*for(int i=0;i<palcount;i++)
            palette[i] = VGA16Color((int)win4bitconv[i]);*/

        switch(bih.Compression)
        {
          case BI_RGB:
          {
            for(Y=(int)(bottomup?bih.Height-1:0);(bottomup?Y>=0:Y<bih.Height);(bottomup?Y--:Y++))
            {
                if(1 != fread((void *)mem_, bytelen, 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                mem = mem_;
                for(X=0;X<bih.Width;X++)
                {
                    retval->data[retval->XRes * Y + X] = palette[(((int)mem[X >> 1] << ((X & 1) << 2)) >> 4) & 0xF];
                }
            }
            break;
          }
          case BI_RLE8:
            {fclose(is); free(mem_); freeImage(retval); return NULL;}
          case BI_RLE4:
          {
            X = 0;
            Y = (int)(bottomup ? bih.Height - 1 : 0);
            for(i=0;i<bih.SizeImage;)
            {
                if(1 != fread((void *)&b, sizeof(b), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                i++;
                if(b)
                {
                    if(1 != fread((void *)&c, sizeof(c), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                    i++;
                    clr1 = palette[(int)c >> 4];
                    clr2 = palette[(int)c & 0xF];
                    while(b--)
                    {
                        retval->data[retval->XRes * Y + X++] = clr1;
                        if(X >= bih.Width)
                        {
                            X = 0;
                            if(bottomup && --Y < 0) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                            else if(++Y >= bih.Height) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                        }
                        retval->data[retval->XRes * Y + X++] = clr2;
                        if(X >= bih.Width)
                        {
                            X = 0;
                            if(bottomup && --Y < 0) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                            else if(++Y >= bih.Height) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                        }
                    }
                }
                else
                {
                    if(1 != fread((void *)&b, sizeof(b), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                    i++;
                    if(b >= 3)
                    {
                        for(cnt=b;cnt--;)
                        {
                            if(1 != fread((void *)&c, sizeof(c), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                            i++;
                            if(!cnt && (b & 1)) continue;
                            retval->data[retval->XRes * Y + X++] = palette[(int)c >> 4];
                            if(X >= bih.Width)
                            {
                                X = 0;
                                if(bottomup && --Y < 0) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                                else if(++Y >= bih.Height) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                            }
                            retval->data[retval->XRes * Y + X++] = palette[(int)c & 0xF];
                            if(X >= bih.Width)
                            {
                                X = 0;
                                if(bottomup && --Y < 0) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                                else if(++Y >= bih.Height) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                            }
                        }
                    }
                    else if(b == 0) // End of Line
                    {
                        X = 0;
                        if(bottomup && --Y < 0) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                        else if(++Y >= bih.Height) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                    }
                    else if(b == 1) // End of Bitmap
                    {
                        if(i > bih.SizeImage) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                        i = bih.SizeImage;
                        break;
                    }
                    else // b == 2 Delta
                    {
                        if(1 != fread((void *)&x, sizeof(x), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                        if(1 != fread((void *)&y, sizeof(y), 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                        X += (int)x;
                        if(X >= bih.Width) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                        if(bottomup && (Y -= (int)y) < 0) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                        else if((Y += (int)y) >= bih.Height) {fclose(is); free(mem_); freeImage(retval); return NULL;}
                    }
                }
            }
            if(i != bih.SizeImage) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            break;
          }
          case BI_BITFIELDS:
            {fclose(is); free(mem_); freeImage(retval); return NULL;}
        }
        break;
      }
      case 24:
      {
        if(bih.Compression != BI_RGB) {fclose(is); free(mem_); freeImage(retval); return NULL;}
        for(Y=(bottomup?bih.Height-1:0);(bottomup?Y>=0:Y<bih.Height);(bottomup?Y--:Y++))
        {
            if(1 != fread((void *)mem_, bytelen, 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            tmem = (struct RGBTri *)mem_;
            for(X=0;X<bih.Width;X++)
            {
                retval->data[retval->XRes * Y + X] = RGB(tmem->R, tmem->G, tmem->B);
                tmem++;
            }
        }
        break;
      }
      case 32:
      {
        if(bih.Compression != BI_RGB) {fclose(is); free(mem_); freeImage(retval); return NULL;}
        for(Y=(bottomup?bih.Height-1:0);(bottomup?Y>=0:Y<bih.Height);(bottomup?Y--:Y++))
        {
            if(1 != fread((void *)mem_, bytelen, 1, is)) {fclose(is); free(mem_); freeImage(retval); return NULL;}
            qmem = (struct RGBQuad *)mem_;
            for(X=0;X<bih.Width;X++)
            {
                retval->data[retval->XRes * Y + X] = RGB(qmem->R, qmem->G, qmem->B);
                qmem++;
            }
        }
        break;
      }
      default:
        {fclose(is); free(mem_); freeImage(retval); return NULL;}
    }
    return retval;
}

struct PACKED VBEInfoBlockType /* must be 256 bytes */
{
    char VESASignature[4]; /* 'VESA' 4 byte signature */
    short VESAVersion; /* VBE version number */
    unsigned long /* char * */ OEMStringPtr; /* Pointer to OEM string */
    long Capabilities; /* Capabilities of video card */
    unsigned long /* unsigned * */ VideoModePtr; /* Pointer to supported modes */
    short TotalMemory; /* Number of 64kb memory blocks */
    char reserved[236]; /* Pad to 256 byte block size */
} VBEInfoBlock;

struct PACKED ModeInfoBlockType /* must be 256 bytes */
{
    unsigned short ModeAttributes; /* Mode attributes */
    unsigned char WinAAttributes; /* Window A attributes */
    unsigned char WinBAttributes; /* Window B attributes */
    unsigned short WinGranularity; /* Window granularity in k */
    unsigned short WinSize; /* Window size in k */
    unsigned short WinASegment; /* Window A segment */
    unsigned short WinBSegment; /* Window B segment */
    /*void (far *WinFuncPtr)(void)*/unsigned long WinFuncPtr; /* Pointer to window function */
    unsigned short BytesPerScanLine; /* Bytes per scanline */
    unsigned short XResolution; /* Horizontal resolution */
    unsigned short YResolution; /* Vertical resolution */
    unsigned char XCharSize; /* Character cell width */
    unsigned char YCharSize; /* Character cell height */
    unsigned char NumberOfPlanes; /* Number of memory planes */
    unsigned char BitsPerPixel; /* Bits per pixel */
    unsigned char NumberOfBanks; /* Number of CGA style banks */
    unsigned char MemoryModel; /* Memory model type */
    unsigned char BankSize; /* Size of CGA style banks */
    unsigned char NumberOfImagePages; /* Number of images pages */
    unsigned char res1; /* Reserved */
    unsigned char RedMaskSize; /* Size of direct color red mask */
    unsigned char RedFieldPosition; /* Bit posn of lsb of red mask */
    unsigned char GreenMaskSize; /* Size of direct color green mask */
    unsigned char GreenFieldPosition; /* Bit posn of lsb of green mask */
    unsigned char BlueMaskSize; /* Size of direct color blue mask */
    unsigned char BlueFieldPosition; /* Bit posn of lsb of blue mask */
    unsigned char RsvdMaskSize; /* Size of direct color res mask */
    unsigned char RsvdFieldPosition; /* Bit posn of lsb of res mask */
    unsigned char DirectColorModeInfo; /* Direct color mode attributes */
    unsigned long PhysBasePtr; /* Physical Address of Framebuffer */
    unsigned char res2[212]; /* Pad to 256 byte block size */
} ModeInfoBlock;
int videoaddr, bankShift, isMode0x13 = 0, isLinear = 0;
void * linearAddr = NULL;
int linearAddrSel = -1;
unsigned short ModeList[512 + 1]; // 512 because that is the maximum number of modes and 1 for the list terminator

unsigned char TextBmp[8 * 256];

void initText()
{
    __dpmi_regs regs;
    int curchar, x, y, curbits;
    unsigned char curpix;
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x13;
    __dpmi_int(0x10, &regs);
    for(curchar=0;curchar<256;curchar++)
    {
        regs.x.ax = 0x200;
        regs.x.bx = 0;
        regs.x.dx = 0;
        __dpmi_int(0x10, &regs);
        regs.x.ax = 0x900 | curchar;
        regs.x.bx = 0x1;
        regs.x.cx = 1;
        __dpmi_int(0x10, &regs);
        for(y=0;y<8;y++)
        {
            curbits = 0;
            for(x=0;x<8;x++)
            {
                dosmemget(0xA0000 + 320 * y + x, sizeof(curpix), &curpix);
                curbits |= (curpix) ? 1 << x : 0;
            }
            TextBmp[curchar * 8 + y] = curbits;
        }
    }
    textmode(C4350);
}

struct polygon
{
    float x1, y1, z1, x2, y2, z2, x3, y3, z3;
    float u1, v1, u2, v2, u3, v3;
    struct Image * image;
};

const unsigned char POLYGONTYPE = 'p', PLANETYPE = 'P';

struct drawpolypolygon
{
    unsigned char type;
    float x1, y1, z1, x2, y2, z2, x3, y3, z3;
    float ua, ub, uc, ud, va, vb, vc, vd;
    Color * image;
    unsigned imageXRes, imageYRes;
};

int ScreenXRes, ScreenYRes;

float * zbuf = NULL;
unsigned char * vbuf = NULL;
struct drawpolypolygon ** tbuf = NULL;
Color * dbuf = NULL;
Color * overlay = NULL;
float YScaleFactor = 1.0;

const int PolyAllocChunk = 256;

struct allocstruct
{
    unsigned indexoffset;
    struct drawpolypolygon polys[256];
    struct allocstruct * next;
};

static struct allocstruct * curdrawpolylisthead = NULL;
static struct allocstruct * curdrawpolylistcurusedpos = NULL;
static int curdrawpolylistused = 0;

static struct drawpolypolygon * alloc_new_drawpolypolygon(void)
{
    struct allocstruct * newnode;
    if(!curdrawpolylisthead)
    {
        curdrawpolylisthead = (struct allocstruct *)malloc(sizeof(struct allocstruct));
        curdrawpolylistcurusedpos = curdrawpolylisthead;
        curdrawpolylisthead->indexoffset = 0;
        curdrawpolylisthead->next = NULL;
    }
    else if(curdrawpolylistused >= PolyAllocChunk + curdrawpolylistcurusedpos->indexoffset)
    {
        if(curdrawpolylistcurusedpos->next)
        {
            curdrawpolylistcurusedpos = curdrawpolylistcurusedpos->next;
        }
        else
        {
            newnode = (struct allocstruct *)malloc(sizeof(struct allocstruct));
            curdrawpolylistcurusedpos->next = newnode;
            newnode->indexoffset = PolyAllocChunk + curdrawpolylistcurusedpos->indexoffset;
            newnode->next = NULL;
            curdrawpolylistcurusedpos = newnode;
        }
    }
    return &curdrawpolylistcurusedpos->polys[curdrawpolylistused++ - curdrawpolylistcurusedpos->indexoffset];
}

const float NearDist = 0.01;

union eightbitcolor
{
    struct
    {
        unsigned R : 3;
        unsigned G : 3;
        unsigned B : 2;
    } c;
    int RGB;
};

void setPalette(int index, int R, int G, int B)
{
    __dpmi_regs regs;
    struct PACKED
    {
        unsigned char B, G, R, Res;
    } c;
    if(isMode0x13)
    {
        outp(0x3C6, 0xFF);
        outp(0x3C8, index);
        outp(0x3C9, R >> 2);
        outp(0x3C9, G >> 2);
        outp(0x3C9, B >> 2);
    }
    else
    {
        memset(&regs, 0, sizeof(regs));
        regs.x.ax = 0x4F09;
        regs.h.bl = 0;
        regs.x.cx = 1; // set 1 palette entry
        regs.x.dx = index & 0xFF;
        regs.x.es = __tb >> 4;
        regs.x.di = __tb & 0xF;
        c.R = R >> 2;
        c.G = G >> 2;
        c.B = B >> 2;
        c.Res = 0;
        dosmemput(&c, sizeof(c), __tb);
        __dpmi_int(0x10, &regs);
    }
}

void init8BitPalette()
{
    union eightbitcolor c;
    int R, G, B;
    for(c.RGB=0;c.RGB<256;c.RGB++)
    {
        R = c.c.R >> 1 | c.c.R << 2 | c.c.R << 5;
        G = c.c.G >> 1 | c.c.G << 2 | c.c.G << 5;
        B = c.c.B | c.c.B << 2 | c.c.B << 4 | c.c.B << 6;
        setPalette(c.RGB, R, G, B);
    }
}

int ColorToScreen8Bit(Color c_in)
{
    union eightbitcolor c;
    c.c.R = GetRValue(c_in) >> 5;
    c.c.G = GetGValue(c_in) >> 5;
    c.c.B = GetBValue(c_in) >> 6;
    return c.RGB;
}

int ColorToScreen16Bit(Color c_in)
{
    int result;
    result = (GetRValue(c_in) >> (8 - ModeInfoBlock.RedMaskSize)) << ModeInfoBlock.RedFieldPosition;
    result |= (GetGValue(c_in) >> (8 - ModeInfoBlock.GreenMaskSize)) << ModeInfoBlock.GreenFieldPosition;
    result |= (GetBValue(c_in) >> (8 - ModeInfoBlock.BlueMaskSize)) << ModeInfoBlock.BlueFieldPosition;
    return result;
}

int ColorToScreen32Bit(Color c)
{
    return c & 0xFFFFFF;
}

int (*CurColorToScreen)(Color c);

int ColorToScreen(Color c)
{
    return (*CurColorToScreen)(c);
}

void InitColorToScreen()
{
    switch(ModeInfoBlock.BitsPerPixel)
    {
    case 8:
        CurColorToScreen = &ColorToScreen8Bit;
        break;
    case 15:
    case 16:
        CurColorToScreen = &ColorToScreen16Bit;
        break;
    case 24:
    case 32:
        CurColorToScreen = &ColorToScreen32Bit;
        break;
    }
}

int SupportedBitsPerPixel()
{
    switch(ModeInfoBlock.BitsPerPixel)
    {
    case 8:
    case 15:
    case 16:
    case 24:
    case 32:
        return 1;
    }
    return 0;
}

#define BytesPerPixel ((ModeInfoBlock.BitsPerPixel + 7) >> 3)
void DumpMem(const void * memptr_in, int length)
{
    const unsigned char * memptr;
    int i, lastwasnl = 0;
    if(!memptr_in) return;
    for(i=0,memptr=memptr_in;i<length;i++,memptr++)
    {
        lastwasnl = 0;
        printf("%02hhX ", *memptr);
        if(i % 16 == 15) {printf("\n"); lastwasnl = 1;}
    }
    if(!lastwasnl) printf("\n");
}

unsigned DOSToLinear(unsigned long dosaddr)
{
    return ((dosaddr & 0xFFFF0000) >> 12) + (dosaddr & 0xFFFF);
}

int getVBEInfo(void)
{
    __dpmi_regs regs;
    int memptr, i;
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x4F00;
    regs.x.di = __tb & 0xF;
    regs.x.es = __tb >> 4;
    __dpmi_int(0x10, &regs);
    if(regs.x.ax == 0x4F)
    {
        dosmemget(__tb, 0x100, &VBEInfoBlock);
        memptr = DOSToLinear(VBEInfoBlock.VideoModePtr);
        for(i=0;;i++,memptr+=2)
        {
            dosmemget(memptr, 2, &ModeList[i]);
            if(ModeList[i] == (unsigned short)-1) break;
        }
        return i > 0;
    }
    return 0;
}

void ErrorExit (const char * msg)
{
    printf("Error : %s!\n\n", msg);
    exit(1);
}

/*void DumpModeInfoBlock(void)
{
#define DUMPVAR(v, type) printf(#v " = " type "\n", ModeInfoBlock.v)
    DUMPVAR(ModeAttributes, "0x%hX");
    DUMPVAR(WinAAttributes, "0x%hhX");
    DUMPVAR(WinBAttributes, "0x%hhX");
    DUMPVAR(WinGranularity, "%hu");
    DUMPVAR(WinSize, "%hu");
    DUMPVAR(WinASegment, "0x%hX");
    DUMPVAR(WinBSegment, "0x%hX");
    DUMPVAR(WinFuncPtr, "0x%08lX");
    DUMPVAR(BytesPerScanLine, "%hu");
#undef DUMPVAR
}*/

int getModeInfo(unsigned short mode)
{
    __dpmi_regs regs;
    memset(&regs, 0, sizeof(regs));
    if (mode < 0x100) return 0; /* Ignore non-VBE modes */
    regs.x.ax = 0x4F01;
    regs.x.cx = mode;
    regs.x.di = __tb & 0xF;
    regs.x.es = __tb >> 4;
    __dpmi_int(0x10, &regs);
    if (regs.x.ax != 0x4F) return 0;

    dosmemget(__tb, 0x100, &ModeInfoBlock);

    // check if compatible :
    // mode is supported by hardware (bit 0 == 1)
    if ((ModeInfoBlock.ModeAttributes & 0x1) == 0x1 && ModeInfoBlock.NumberOfPlanes == 1)
        return SupportedBitsPerPixel();
//    printf("    0x%hX : \n", mode);
//    DumpModeInfoBlock();
    return 0;
}

int getVBEMode()
{
    __dpmi_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x4F03;
    __dpmi_int(0x10, &regs);
    return regs.x.bx;
}

void setVBEMode(int mode)
{
    __dpmi_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.x.ax = 0x4F02;
    regs.x.bx = mode;
    __dpmi_int(0x10, &regs);
}

void setBank(int bank)
{
    __dpmi_regs regs;
    if(isMode0x13) return;
    memset(&regs, 0, sizeof(regs));
    regs.x.cs = ModeInfoBlock.WinFuncPtr >> 0x10;
    regs.x.ip = ModeInfoBlock.WinFuncPtr & 0xFFFF;
    regs.x.bx = 0;
    regs.x.dx = bank << bankShift;
    __dpmi_simulate_real_mode_procedure_retf(&regs);
    memset(&regs, 0, sizeof(regs));
    regs.x.cs = ModeInfoBlock.WinFuncPtr >> 0x10;
    regs.x.ip = ModeInfoBlock.WinFuncPtr & 0xFFFF;
    regs.x.bx = 1;
    regs.x.dx = bank << bankShift;
    __dpmi_simulate_real_mode_procedure_retf(&regs);
}

void NoMem(void);

void initmode0x13()
{
    union REGS regs;

    isMode0x13 = 1;
    ModeInfoBlock.BitsPerPixel = 8;
    videoaddr = 0xA0000;
    ModeInfoBlock.BytesPerScanLine = 320;
    ScreenXRes = 320;
    ScreenYRes = 200;
    zbuf = (float *)malloc(sizeof(float) * ScreenXRes * ScreenYRes);
    if(!zbuf) NoMem();
    tbuf = (struct drawpolypolygon **)malloc(sizeof(struct drawpolypolygon *) * ScreenXRes * ScreenYRes);
    if(!tbuf) NoMem();
    dbuf = (Color *)malloc(sizeof(Color) * ScreenXRes * ScreenYRes + 256);
    if(!dbuf) NoMem();
    overlay = (Color *)malloc(sizeof(Color) * ScreenXRes * ScreenYRes + 256);
    if(!overlay) NoMem();

    regs.h.ah = 0;
    regs.h.al = 0x13;
    int86(0x10, &regs, &regs);
    curdrawpolylisthead = NULL;
    curdrawpolylistcurusedpos = NULL;
    curdrawpolylistused = 0;
    init8BitPalette();
    InitColorToScreen();
}

void init(int usevga)
{
    unsigned short modenum = 0;
    unsigned pickedmodenum;
    int colcount=0;
    unsigned short * pmodelist;
    __dpmi_meminfo meminfo;

    initText();

    curdrawpolylisthead = NULL;
    curdrawpolylistcurusedpos = NULL;
    curdrawpolylistused = 0;
    zbuf = NULL;
    tbuf = NULL;
    dbuf = NULL;
    overlay = NULL;

    isMode0x13 = 0;

    if(usevga || !getVBEInfo())
    {
        if(!usevga)
        {
            printf("No VESA VBE detected");
            delay(1000);
        }
        initmode0x13();
        return;
    }

    printf("%c%c%c%c : Version %hX.%hX\n", VBEInfoBlock.VESASignature[0], VBEInfoBlock.VESASignature[1], VBEInfoBlock.VESASignature[2], VBEInfoBlock.VESASignature[3], VBEInfoBlock.VESAVersion >> 8, VBEInfoBlock.VESAVersion & 0xFF);

    do
    {
        printf("\nVideo Modes : \n    ");

        for(pmodelist=ModeList;*pmodelist!=(unsigned short)-1;pmodelist++)
        {
            modenum = *pmodelist;
            if(!getModeInfo(modenum)) continue;
            printf("0x%X : %ix%i at %ibpp", modenum, ModeInfoBlock.XResolution, ModeInfoBlock.YResolution, ModeInfoBlock.BitsPerPixel);
            if(++colcount >= 2)
            {
                colcount = 0;
                printf("\n    ");
            }
            else
            {
                printf("\t\t");
            }
        }
        printf("\nSelect one or 0 to exit: 0x");
        scanf("%x", &pickedmodenum);
        while(kbhit()) getch();
        if(pickedmodenum == 0)
        {
            exit(0);
        }
        for(pmodelist=ModeList;*pmodelist!=(unsigned short)-1;pmodelist++)
        {
            modenum = *pmodelist;
            if(!getModeInfo(modenum)) continue;
            if(modenum == pickedmodenum) break;
        }
        if(*pmodelist == (unsigned short)-1)
        {
            printf("\nIllegal Mode! : 0x%X\n\nTry again.\n", pickedmodenum);
        }
    }
    while(*pmodelist == (unsigned short)-1);
    if(modenum == 0) exit(0);
    ScreenXRes = ModeInfoBlock.XResolution;
    ScreenYRes = ModeInfoBlock.YResolution;
    isLinear = (ModeInfoBlock.ModeAttributes & 0x80) ? 1 : 0;



    if(isLinear)
    {
        meminfo.address = ModeInfoBlock.PhysBasePtr;
        meminfo.size = ScreenYRes * ModeInfoBlock.BytesPerScanLine;
        __dpmi_physical_address_mapping(&meminfo);
        linearAddr = (void *)meminfo.address;
        linearAddrSel = __dpmi_allocate_ldt_descriptors(1);
        __dpmi_set_segment_base_address(linearAddrSel, (unsigned)linearAddr);
        __dpmi_set_segment_limit(linearAddrSel, (1 << 28) - 1);
        modenum |= 0x4000; // set linear mode bit
    }
    else
    {
        if((ModeInfoBlock.WinAAttributes & 7) == 7 && ModeInfoBlock.WinASegment) // windowed, readable, writable
            videoaddr = (unsigned)ModeInfoBlock.WinASegment << 4;
        else if((ModeInfoBlock.WinBAttributes & 7) == 7 && ModeInfoBlock.WinBSegment) // windowed, readable, writable
            videoaddr = (unsigned)ModeInfoBlock.WinBSegment << 4;
        while(64 >> bankShift != (int)ModeInfoBlock.WinGranularity && bankShift <= 6)
            bankShift++;
    }

    setVBEMode(modenum);

    if(ModeInfoBlock.BitsPerPixel == 8) init8BitPalette();

    InitColorToScreen();

    zbuf = (float *)malloc(sizeof(float) * ScreenXRes * ScreenYRes);
    if(!zbuf) NoMem();
    tbuf = (struct drawpolypolygon **)malloc(sizeof(struct drawpolypolygon *) * ScreenXRes * ScreenYRes);
    if(!tbuf) NoMem();
    dbuf = (Color *)malloc(sizeof(Color) * ScreenXRes * ScreenYRes + 256 /* buffer space : DO NOT REMOVE */);
    if(!dbuf) NoMem();
    overlay = (Color *)malloc(sizeof(Color) * ScreenXRes * ScreenYRes + 256);
    if(!overlay) NoMem();
}

static void FreeUsedImages()
{
    int i;
    struct allocstruct * curptr = curdrawpolylisthead;
    for(i = 0; i < curdrawpolylistused; i++)
    {
        if(i >= PolyAllocChunk + curptr->indexoffset)
            curptr = curptr->next;
//        Image_Free(curptr->polys[i - curptr->indexoffset].imageref);
    }
    curdrawpolylistused = 0;
    curdrawpolylistcurusedpos = curdrawpolylisthead;
}

void term(void)
{
    FreeUsedImages();
    free(zbuf);
    free(tbuf);
    free(dbuf);
    while(curdrawpolylisthead)
    {
        struct allocstruct * nextnode = curdrawpolylisthead->next;
        free(curdrawpolylisthead);
        curdrawpolylisthead = nextnode;
    }
    free(overlay);
    if(linearAddrSel != -1) __dpmi_free_ldt_descriptor(linearAddrSel);
    textmode(C4350);
}

void NoMem(void)
{
    term();
    printf("Out of memory!\nPress a key to exit...\n");
    while(kbhit()) getch();
    if(!getch()) getch();
    exit(1);
}

void CrossProduct(float * rX, float * rY, float * rZ, float ax, float ay, float az, float bx, float by, float bz)
{
    *rX = ay * bz - az * by;
    *rY = az * bx - ax * bz;
    *rZ = ax * by - ay * bx;
}

int getabcd(float * a, float * b, float * c, float * d, float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3)
{
    float cx, cy, cz;
    *a = y3 * (z2 - z1) + y2 * (z1 - z3) + y1 * (z3 - z2);
    *b = x3 * (z1 - z2) + x1 * (z2 - z3) + x2 * (z3 - z1);
    *c = x3 * (y2 - y1) + x2 * (y1 - y3) + x1 * (y3 - y2);
    *d = -x3 * y2 * z1 + x2 * y3 * z1 + x3 * y1 * z2 - x1 * y3 * z2 - x2 * y1 * z3 + x1 * y2 * z3;
    if(*d == 0.0) return 0;

    CrossProduct(&cx, &cy, &cz, x1 - x3, y1 - y3, z1 - z3, x1 - x2, y1 - y2, z1 - z2);
    if(cx == 0 && cy == 0 && cz == 0) return 0;
    return 1;
}

void GetLineEq(float * a, float * b, float * c, float x1, float y1, float x2, float y2, float avgx, float avgy)
{
    *a = y1 - y2;
    *b = x2 - x1;
    *c = x1 * y2 - x2 * y1;
    if(*a * avgx + *b * avgy + *c < 0.0)
    {
        *a = -*a;
        *b = -*b;
        *c = -*c;
    }
}

float DotProduct(float ax, float ay, float az, float bx, float by, float bz)
{
    return ax * bx + ay * by + az * bz;
}

void Normalize(float * x, float * y, float * z)
{
    float r = sqrt(*x * *x + *y * *y + *z * *z);
    if(r != 0 && r != 1)
    {
        r = 1.0 / r;
        *x *= r;
        *y *= r;
        *z *= r;
    }
}

void transformuv(float * ua, float * ub, float * uc, float * ud, float * va, float * vb, float * vc, float * vd, const struct polygon * p)
{
    float uanew, ubnew, ucnew, udnew;
    float vanew, vbnew, vcnew, vdnew;
    uanew = (-p->u1 + p->u3) * *ua + (-p->u1 + p->u2) * *va;
    ubnew = (-p->u1 + p->u3) * *ub + (-p->u1 + p->u2) * *vb;
    ucnew = (-p->u1 + p->u3) * *uc + (-p->u1 + p->u2) * *vc;
    udnew = p->u1 + (-p->u1 + p->u3) * *ud + (-p->u1 + p->u2) * *vd;
    vanew = *ua * (-p->v1 + p->v3) + (-p->v1 + p->v2) * *va;
    vbnew = *ub * (-p->v1 + p->v3) + (-p->v1 + p->v2) * *vb;
    vcnew = *uc * (-p->v1 + p->v3) + (-p->v1 + p->v2) * *vc;
    vdnew = p->v1 + *ud * (-p->v1 + p->v3) + (-p->v1 + p->v2) * *vd;

    *ua = uanew;
    *ub = ubnew;
    *uc = ucnew;
    *ud = udnew;
    *va = vanew;
    *vb = vbnew;
    *vc = vcnew;
    *vd = vdnew;
}

void drawpoly(const struct polygon * polyin)
{
    float a, b, c, d;
    float NormX, NormY, NormZ;
    float p2v, p3u;
    float a1, b1, c1, a2, b2, c2, a3, b3, c3;
    float x1, y1, x2, y2, x3, y3;
    float fminx, fmaxx, fminy, fmaxy;
    int minx, miny, maxx, maxy;
    float b1_y, b2_y, b3_y, ia1_x, ia2_x, ia3_x;
    float dx, ix, x, dy, iy, y;
    int xi, yi;
    float d1, d2, d3;
    float * pzbuf;
    struct drawpolypolygon ** ptexbuf;
    struct drawpolypolygon * p;
    float curz;
    int pos, finishx;

    p = alloc_new_drawpolypolygon();
    p->x1 = polyin->x1;
    p->y1 = polyin->y1;
    p->z1 = polyin->z1;
    p->x2 = polyin->x2;
    p->y2 = polyin->y2;
    p->z2 = polyin->z2;
    p->x3 = polyin->x3;
    p->y3 = polyin->y3;
    p->z3 = polyin->z3;
    p->image = polyin->image->data;
    p->imageXRes = polyin->image->XRes;
    p->imageYRes = polyin->image->YRes;
    p->type = POLYGONTYPE;

    if(p->z1 < NearDist || p->z2 < NearDist || p->z3 < NearDist)
        return;

    if(!getabcd(&a, &b, &c, &d, p->x1, p->y1, p->z1, p->x2, p->y2, p->z2, p->x3, p->y3, p->z3))
        return;
    CrossProduct(&NormX, &NormY, &NormZ, p->x2 - p->x1, p->y2 - p->y1, p->z2 - p->z1, p->x3 - p->x1, p->y3 - p->y1, p->z3 - p->z1);
    Normalize(&NormX, &NormY, &NormZ);
    if(NormX == 0 && NormY == 0 && NormZ == 0)
        return;

    getabcd(&p->va, &p->vb, &p->vc, &p->vd, p->x1, p->y1, p->z1, p->x1 + NormX, p->y1 + NormY, p->z1 + NormZ, p->x3, p->y3, p->z3);
    p2v = p->va * p->x2 + p->vb * p->y2 + p->vc * p->z2 + p->vd;
    if(p2v == 0)
        return;
    p->va /= p2v;
    p->vb /= p2v;
    p->vc /= p2v;
    p->vd /= p2v;

    getabcd(&p->ua, &p->ub, &p->uc, &p->ud, p->x1, p->y1, p->z1, p->x1 + NormX, p->y1 + NormY, p->z1 + NormZ, p->x2, p->y2, p->z2);
    p3u = p->ua * p->x3 + p->ub * p->y3 + p->uc * p->z3 + p->ud;
    if(p3u == 0)
        return;
    p->ua /= p3u;
    p->ub /= p3u;
    p->uc /= p3u;
    p->ud /= p3u;

    transformuv(&p->ua, &p->ub, &p->uc, &p->ud, &p->va, &p->vb, &p->vc, &p->vd, polyin);

    p->ua *= p->imageXRes - 1;
    p->ub *= p->imageXRes - 1;
    p->uc *= p->imageXRes - 1;
    p->ud *= p->imageXRes - 1;

    p->va = -p->va;
    p->vb = -p->vb;
    p->vc = -p->vc;
    p->vd = 1 - p->vd;

    p->va *= p->imageYRes - 1;
    p->vb *= p->imageYRes - 1;
    p->vc *= p->imageYRes - 1;
    p->vd *= p->imageYRes - 1;

    x1 = ScreenXRes / 2 + ScreenXRes / 2 * (p->x1 / p->z1);
    y1 = ScreenYRes / 2 - ScreenYRes / 2 * (p->y1 / p->z1) * YScaleFactor;

    x2 = ScreenXRes / 2 + ScreenXRes / 2 * (p->x2 / p->z2);
    y2 = ScreenYRes / 2 - ScreenYRes / 2 * (p->y2 / p->z2) * YScaleFactor;

    x3 = ScreenXRes / 2 + ScreenXRes / 2 * (p->x3 / p->z3);
    y3 = ScreenYRes / 2 - ScreenYRes / 2 * (p->y3 / p->z3) * YScaleFactor;


    GetLineEq(&a1, &b1, &c1, x1, y1, x2, y2, x3, y3);
    GetLineEq(&a2, &b2, &c2, x2, y2, x3, y3, x1, y1);
    GetLineEq(&a3, &b3, &c3, x3, y3, x1, y1, x2, y2);

    fminx = x1;
    fmaxx = x1;
    fminy = y1;
    fmaxy = y1;
    if(x2 < fminx)
        fminx = x2;
    else if(x2 > fmaxx)
        fmaxx = x2;

    if(y2 < fminy)
        fminy = y2;
    else if(y2 > fmaxy)
        fmaxy = y2;

    if(x3 < fminx)
        fminx = x3;
    else if(x3 > fmaxx)
        fmaxx = x3;

    if(y3 < fminy)
        fminy = y3;
    else if(y3 > fmaxy)
        fmaxy = y3;

    if(fminx < 0)
        fminx = 0;

    if(fmaxx > ScreenXRes - 1.0)
        fmaxx = ScreenXRes - 1.0;

    if(fminy < 0)
        fminy = 0;

    if(fmaxy > ScreenYRes - 1.0)
        fmaxy = ScreenYRes - 1.0;

    minx = (int)floor(fminx);
    maxx = (int)ceil(fmaxx);
    miny = (int)floor(fminy);
    maxy = (int)ceil(fmaxy);

    b1_y = miny * b1;
    b2_y = miny * b2;
    b3_y = miny * b3;

    ia1_x = minx * a1;
    ia2_x = minx * a2;
    ia3_x = minx * a3;

    a /= -d;
    b /= -d;
    c /= -d;
    d = -1.0;

    dx = 2.0 * a / ScreenXRes;
    ix = 2.0 * a * (minx - ScreenXRes / 2.0) / ScreenXRes;
    dy = -2.0 * b / ScreenYRes / YScaleFactor;
    iy = -2.0 * b * (miny - ScreenYRes / 2.0) / ScreenYRes / YScaleFactor;
    y = iy + c;

    for(yi=miny;yi<=maxy;yi++)
    {
        d1 = ia1_x + b1_y + c1;
        d2 = ia2_x + b2_y + c2;
        d3 = ia3_x + b3_y + c3;
        pzbuf = &zbuf[minx + ScreenXRes * yi];
        ptexbuf = &tbuf[minx + ScreenXRes * yi];
        x = ix + y;
        curz = x;
        xi = minx;
        if(a1 > 0 && d1 < 0)
        {
            pos = (int)ceil(d1 / -a1) + minx;
            if(xi < pos) xi = pos;
        }
        if(a2 > 0 && d2 < 0)
        {
            pos = (int)ceil(d2 / -a2) + minx;
            if(xi < pos) xi = pos;
        }
        if(a3 > 0 && d3 < 0)
        {
            pos = (int)ceil(d3 / -a3) + minx;
            if(xi < pos) xi = pos;
        }
        d1 += a1 * (xi - minx);
        d2 += a2 * (xi - minx);
        d3 += a3 * (xi - minx);
        for(;xi<=maxx;xi++)
        {
            if(d1 < 0 && a1 < 0)
                break;
            if(d2 < 0 && a2 < 0)
                break;
            if(d3 < 0 && a3 < 0)
                break;
            if(d1 >= 0 && d2 >= 0 && d3 >= 0)
                break;
            d1 += a1;
            d2 += a2;
            d3 += a3;
        }
        if((d1 > 0 || a1 > 0) && (d2 > 0 || a2 > 0) && (d3 > 0 || a3 > 0))
        {
            curz += dx * (xi - minx);
            pzbuf += xi - minx;
            ptexbuf += xi - minx;

            finishx = maxx;
            if(a1 < 0 && d1 > 0)
            {
                pos = (int)floor(d1 / -a1) + xi;
                if(finishx > pos) finishx = pos;
            }
            if(a2 < 0 && d2 > 0)
            {
                pos = (int)floor(d2 / -a2) + xi;
                if(finishx > pos) finishx = pos;
            }
            if(a3 < 0 && d3 > 0)
            {
                pos = (int)floor(d3 / -a3) + xi;
                if(finishx > pos) finishx = pos;
            }

            for(;xi<=finishx;xi++)
            {
//                if(d1 < 0 || d2 < 0 || d3 < 0)
//                    break;
                if(curz > *pzbuf) // if closer
                {
                    *ptexbuf = p;
                    *pzbuf = curz;
                }
//                d1 += a1;
//                d2 += a2;
//                d3 += a3;
                curz += dx;
                pzbuf++;
                ptexbuf++;
            }
        }

        b1_y += b1;
        b2_y += b2;
        b3_y += b3;
        y += dy;
    }
}

void drawplane(const struct polygon * polyin)
{
    float a, b, c, d;
    float NormX, NormY, NormZ;
    float p2v, p3u;
    int minx, miny, maxx, maxy;
    float dx, ix, x, dy, iy, y;
    int xi, yi;
    float * pzbuf;
    struct drawpolypolygon ** ptexbuf;
    float curz;
    struct drawpolypolygon * p;

    p = alloc_new_drawpolypolygon();
    p->x1 = polyin->x1;
    p->y1 = polyin->y1;
    p->z1 = polyin->z1;
    p->x2 = polyin->x2;
    p->y2 = polyin->y2;
    p->z2 = polyin->z2;
    p->x3 = polyin->x3;
    p->y3 = polyin->y3;
    p->z3 = polyin->z3;
    p->image = polyin->image->data;
    p->imageXRes = polyin->image->XRes;
    p->imageYRes = polyin->image->YRes;
    p->type = PLANETYPE;

    if(p->z1 < NearDist || p->z2 < NearDist || p->z3 < NearDist)
        return;

    if(!getabcd(&a, &b, &c, &d, p->x1, p->y1, p->z1, p->x2, p->y2, p->z2, p->x3, p->y3, p->z3))
        return;
    CrossProduct(&NormX, &NormY, &NormZ, p->x2 - p->x1, p->y2 - p->y1, p->z2 - p->z1, p->x3 - p->x1, p->y3 - p->y1, p->z3 - p->z1);
    Normalize(&NormX, &NormY, &NormZ);
    if(NormX == 0 && NormY == 0 && NormZ == 0)
        return;

    getabcd(&p->va, &p->vb, &p->vc, &p->vd, p->x1, p->y1, p->z1, p->x1 + NormX, p->y1 + NormY, p->z1 + NormZ, p->x3, p->y3, p->z3);
    p2v = p->va * p->x2 + p->vb * p->y2 + p->vc * p->z2 + p->vd;
    if(p2v == 0)
        return;
    p->va /= p2v;
    p->vb /= p2v;
    p->vc /= p2v;
    p->vd /= p2v;

    getabcd(&p->ua, &p->ub, &p->uc, &p->ud, p->x1, p->y1, p->z1, p->x1 + NormX, p->y1 + NormY, p->z1 + NormZ, p->x2, p->y2, p->z2);
    p3u = p->ua * p->x3 + p->ub * p->y3 + p->uc * p->z3 + p->ud;
    if(p3u == 0)
        return;
    p->ua /= p3u;
    p->ub /= p3u;
    p->uc /= p3u;
    p->ud /= p3u;

    transformuv(&p->ua, &p->ub, &p->uc, &p->ud, &p->va, &p->vb, &p->vc, &p->vd, polyin);

    p->ua *= p->imageXRes - 1;
    p->ub *= p->imageXRes - 1;
    p->uc *= p->imageXRes - 1;
    p->ud *= p->imageXRes - 1;

    p->va = -p->va;
    p->vb = -p->vb;
    p->vc = -p->vc;
    p->vd = 1 - p->vd;

    p->va *= p->imageYRes - 1;
    p->vb *= p->imageYRes - 1;
    p->vc *= p->imageYRes - 1;
    p->vd *= p->imageYRes - 1;

    minx = 0;
    maxx = ScreenXRes - 1;
    miny = 0;
    maxy = ScreenYRes - 1;

    a /= -d;
    b /= -d;
    c /= -d;
    d = -1.0;

    dx = 2.0 * a / ScreenXRes;
    ix = 2.0 * a * (minx - ScreenXRes / 2.0) / ScreenXRes;
    dy = -2.0 * b / ScreenYRes / YScaleFactor;
    iy = -2.0 * b * (miny - ScreenYRes / 2.0) / ScreenYRes / YScaleFactor;
    y = iy + c;

    for(yi=miny;yi<=maxy;yi++)
    {
        pzbuf = &zbuf[minx + ScreenXRes * yi];
        ptexbuf = &tbuf[minx + ScreenXRes * yi];
        x = ix + y;
        curz = x;
        xi = minx;
        {
            for(;xi<=maxx;xi++)
            {
                if(curz > *pzbuf) // if closer
                {
                    *ptexbuf = p;
                    *pzbuf = curz;
                }
                curz += dx;
                pzbuf++;
                ptexbuf++;
            }
        }

        y += dy;
    }
}

int didupdatetext = 1, usedidupdatetext = 0;

void update(Color background)
{
    int writeaddr, LineSize, curbank = -1, bank;
    int xi, yi;
    char * pdbuf;
#if 1
    float dx, dy, xv, yv;
    struct drawpolypolygon ** ptexbuf;
    float * pzbuf;
    struct drawpolypolygon * pp;
    float z;
    float u, v;
    int ImgX, ImgY;
    Color color;
    Color * poverlay;
    int backgroundscreenclr;

    backgroundscreenclr = ColorToScreen(background);
    dx = 2.0 / ScreenXRes;
    dy = -2.0 / ScreenYRes / YScaleFactor;
    yv = 1.0 / YScaleFactor;
    for(yi=0;yi<ScreenYRes;yi++,yv+=dy)
    {
        pdbuf = (char *)&dbuf[yi * ScreenXRes];
        poverlay = &overlay[yi * ScreenXRes];
        ptexbuf = &tbuf[yi * ScreenXRes];
        pzbuf = &zbuf[yi * ScreenXRes];
        xv = -1.0;
        for(xi=0;xi<ScreenXRes;xi++,xv+=dx,pdbuf+=BytesPerPixel,ptexbuf++,pzbuf++,poverlay++)
        {
            if(*poverlay != OVERLAY_TRANSPARENT)
            {
                *(long *)pdbuf = ColorToScreen(*poverlay);
                continue;
            }
            *(long *)pdbuf = backgroundscreenclr;
            pp = *ptexbuf;
            if(!pp) continue;
            z = *pzbuf;
            if(z <= 0) continue;
            z = 1 / z;
            u = (pp->ua * xv + pp->ub * yv + pp->uc) * z + pp->ud;
            v = (pp->va * xv + pp->vb * yv + pp->vc) * z + pp->vd;
            if(pp->type == PLANETYPE)
            {
                if(u > 0)
                    u = fmod(u, pp->imageXRes);
                else
                    u = pp->imageXRes - fmod(pp->imageXRes - u, pp->imageXRes);
                if(v > 0)
                    v = fmod(v, pp->imageYRes);
                else
                    v = pp->imageYRes - fmod(pp->imageYRes - v, pp->imageYRes);
            }
            else// if(pp->type == POLYGONTYPE)
            {
                if(u < 0)
                    u = 0;
                else if(u > pp->imageXRes - 1)
                    u = pp->imageXRes - 1;
                if(v < 0)
                    v = 0;
                else if(v > pp->imageYRes - 1)
                    v = pp->imageYRes - 1;
            }
            ImgX = (int)u;
            ImgY = (int)v;
            color = RGB(0, 0, 0);
//            if(ImgX >= 0 && ImgX < pp->imageXRes && ImgY >= 0 && ImgY < pp->imageYRes)
                color = pp->image[ImgX + ImgY * pp->imageXRes];
            *(long *)pdbuf = ColorToScreen(color);
        }
    }
#endif
#if 1
    if(!didupdatetext && usedidupdatetext) return;
    didupdatetext = 0;
    LineSize = ScreenXRes * BytesPerPixel;
    for(writeaddr=0,yi=0;yi<ScreenYRes;yi++,writeaddr+=(int)ModeInfoBlock.BytesPerScanLine)
    {
        pdbuf = (char *)&dbuf[yi * ScreenXRes];
        if(isLinear)
        {
            movedata(_go32_my_ds(), (unsigned)pdbuf, linearAddrSel, writeaddr, LineSize);
        }
        else
        {
            if(writeaddr >> 16 == (writeaddr + LineSize) >> 16)
            {
                bank = writeaddr >> 16;
                if(bank != curbank)
                {
                    curbank = bank;
                    setBank(bank);
                }
                dosmemput((void *)pdbuf, LineSize, (writeaddr & 0xFFFF) + videoaddr);
            }
            else
            {
                for(xi=0;xi<ScreenXRes;xi++,pdbuf+=BytesPerPixel)
                {
                    bank = (writeaddr + xi * BytesPerPixel) >> 16;
                    if(bank != curbank)
                    {
                        curbank = bank;
                        setBank(bank);
                    }
                    dosmemput((void *)pdbuf, BytesPerPixel, ((writeaddr + xi * BytesPerPixel) & 0xFFFF) + videoaddr);
                }
            }
        }
    }
#endif
}

void clear(void)
{
    int i;
    for(i=0;i<ScreenXRes*ScreenYRes;i++)
    {
        zbuf[i] = 0.0;
        tbuf[i] = NULL;
        dbuf[i] = 0;
        overlay[i] = OVERLAY_TRANSPARENT;
    }
    FreeUsedImages();
}

void SetOverlayPixel(int X, int Y, Color clr)
{
    if(X < 0 || X >= ScreenXRes || Y < 0 || Y >= ScreenYRes) return;
    overlay[X + Y * ScreenXRes] = clr;
}

void DrawOverlayChar(int StX, int StY, Color clr, char ch)
{
    int x, y, charindex;
    charindex = (int)(unsigned char)ch;
    for(y=0;y<8;y++)
    {
        for(x=0;x<8;x++)
        {
            if(TextBmp[charindex * 8 + y] & (1 << x))
                SetOverlayPixel(x + StX, y + StY, clr);
        }
    }
}

void DrawOverlayText(int StX, int StY, Color clr, const char * text)
{
    const int charwidth = 8;
    for(;*text;text++,StX+=charwidth)
    {
        DrawOverlayChar(StX, StY, clr, *text);
    }
}

float frand(void)
{
    return 2 * (float)rand() / RAND_MAX - 1;
}

#ifndef GENERATE_OBJECTS
#define SKIP_DEFINE_POLYGON
#define numpolys polygoncount
#define polygon struct polygon
#include "earth.h"
#undef polygon
#undef numpolys
#else
#define polygonallocstep 256
int polygoncount = 0, polygonalloccount = 0;
struct polygon * polys = NULL;
void errusage(const char * msg);
void addpolygontopolys(struct polygon p)
{
    struct polygon * oldpolys = polys;
    int i;
    if(polygonalloccount <= polygoncount)
    {
        polys = (struct polygon *)malloc(sizeof(struct polygon) * (polygonalloccount += polygonallocstep));
        if(!polys) {errusage("Out of memory!");}
        for(i=0;i<polygoncount;i++)
            polys[i] = oldpolys[i];
        free((void *)oldpolys);
    }
    polys[polygoncount++] = p;
}

void sphereshapefn(float * px, float * py, float * pz, float u, float v)
{
    const float sphererad = 10.0;
    u *= 2 * M_PI;
    v *= M_PI;
    *px = sin(u) * sin(v) * sphererad;
    *py = cos(u) * sin(v) * sphererad;
    *pz =         -cos(v) * sphererad;
}

void boxshapefn(float * px, float * py, float * pz, float u, float v)
{
    const float sphererad = 6.0;
    u *= 2 * M_PI;
    v *= M_PI;
    *px = sin(u) * sin(v) * sphererad;
    *py = cos(u) * sin(v) * sphererad;
    *pz =         -cos(v) * sphererad;
    float maxv = fabs(*px);
    if(maxv < fabs(*py)) maxv = fabs(*py);
    if(maxv < fabs(*pz)) maxv = fabs(*pz);
    maxv /= sphererad;
    *px /= maxv;
    *py /= maxv;
    *pz /= maxv;
}

void torusshapefn(float * px, float * py, float * pz, float u, float v)
{
    const float sphererad = 10.0;
    u *= 2 * M_PI;
    v *= 2 * M_PI;
    float r = 0.25 * cos(v) + 0.75;
    *pz = -0.25 * sin(v) * sphererad;
    *px = sin(u) * r * sphererad;
    *py = cos(u) * r * sphererad;
}

void initobjects(void)
{
    int i, j;
    float u, v;
    const int ucount = 80, vcount = 80;
    struct polygon p;
    p.image = NULL;
    void (*shapefn)(float *, float *, float *, float, float);
    shapefn = &torusshapefn;
    for(i=0;i<ucount;i++)
    {
        for(j=0;j<vcount;j++)
        {
            p.u1 = u = (float)(i + 0) / ucount;
            p.v1 = v = (float)(j + 0) / vcount;
            (*shapefn)(&p.x1, &p.y1, &p.z1, u, v);

            p.u2 = u = (float)(i + 0) / ucount;
            p.v2 = v = (float)(j + 1) / vcount;
            (*shapefn)(&p.x2, &p.y2, &p.z2, u, v);

            p.u3 = u = (float)(i + 1) / ucount;
            p.v3 = v = (float)(j + 0) / vcount;
            (*shapefn)(&p.x3, &p.y3, &p.z3, u, v);
            addpolygontopolys(p);

            p.u1 = u = (float)(i + 1) / ucount;
            p.v1 = v = (float)(j + 1) / vcount;
            (*shapefn)(&p.x1, &p.y1, &p.z1, u, v);

            p.u2 = u = (float)(i + 0) / ucount;
            p.v2 = v = (float)(j + 1) / vcount;
            (*shapefn)(&p.x2, &p.y2, &p.z2, u, v);

            p.u3 = u = (float)(i + 1) / ucount;
            p.v3 = v = (float)(j + 0) / vcount;
            (*shapefn)(&p.x3, &p.y3, &p.z3, u, v);
            addpolygontopolys(p);
        }
    }
}
#endif

typedef void (*drawfntype)(const struct polygon * p);

#ifndef min
#define min(a, b) (((a) > (b)) ? (b) : (a))
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

void errusage(const char * msg)
{
    printf("Error : %s\n"
           "\n"
           "Usage test3d [-p <planeimage.bmp>] [-s <sphereimage.bmp>] -v\n"
           "Options : \n"
           "-p <planeimage.bmp>        Use <planeimage.bmp> for the\n"
           "                           image to texture the plane with.\n"
           "-s <sphereimage.bmp>       Use <sphereimage.bmp> for the\n"
           "                           image to texture the sphere with.\n"
           "                           It uses the mercator projection.\n"
           "-v                         Use VGA Mode 0x13 instead of VESA.\n", msg);
    exit(1);
}

int main(int argc, char ** argv)
{
    const float xanginc = (M_PI * 2 / 360) * 30;
    const float waveanginc = (M_PI * 2 / 360) * 40;
    const float propanginc = M_PI * 2 / 4;
    int i;
    float x, y, z;
    int frames = 0;
    clock_t starttime;
    float xang = 0, zang = 0, waveang = 0, propang = 0;
    float fps = 50;
    drawfntype drawfn;
    struct polygon p;
    const float scalefactor = 1.5;
    float ytrans = 10.0;
    //float a, b, c, d;
    float lx = -1, ly = 1, lz = -1;
    struct Image * knoppiximg;
    struct Image * earthimg;
    char planeimgname[256] = "knoppix.bmp";
    char sphereimgname[256] = "earth.bmp";
    char fpsbuf[256] = "FPS : ?";
    int done = 0;
    int usevga = 0;
    char polycountbuf[256] = "";

    Normalize(&lx, &ly, &lz);
    for(i=1;i<argc;i++)
    {
        if(strcmp(argv[i], "-p") == 0)
        {
            if(i++ == argc - 1) errusage("missing file name for -p");
            strcpy(planeimgname, argv[i]);
        }
        else if(strcmp(argv[i], "-s") == 0)
        {
            if(i++ == argc - 1) errusage("missing file name for -s");
            strcpy(sphereimgname, argv[i]);
        }
        else if(strcmp(argv[i], "-v") == 0)
            usevga = 1;
        else
            errusage("invalid option");
    }

    knoppiximg = loadBMPImage(planeimgname);
    if(!knoppiximg)
    {
        printf("Error : can't load %s!\n", planeimgname);
        exit(1);
    }
    earthimg = loadBMPImage(sphereimgname);
    if(!earthimg)
    {
        freeImage(earthimg);
        printf("Error : can't load %s!\n", sphereimgname);
        exit(1);
    }

#ifdef GENERATE_OBJECTS
    initobjects();
    sprintf(polycountbuf, "%u polygons", (unsigned)polygoncount);
#endif

    init(usevga);

    starttime = clock();

    while(!done)
    {
        if(kbhit())
        {
            switch(getch())
            {
            case '\0':
                getch();
                done = 1;
                break;
            case 'd':
                usedidupdatetext = !usedidupdatetext;
                break;
            default:
                done = 1;
                break;
            }
        }
        frames++;
        if(clock() - CLOCKS_PER_SEC * 3 >= starttime)
        {
            fps = 0.5 * fps + 0.5 * frames / 3.0;
            sprintf(fpsbuf, "FPS : %i%s", frames / 3, ((frames % 3 != 0) ? ((frames % 3 == 1) ? ".3" : ".6") : ""));
            starttime = clock();
            frames = 0;
            didupdatetext = 1;
        }
        clear();
        DrawOverlayText(0, 8, RGB(255, 0, 255), fpsbuf);
        DrawOverlayText(0, 0, RGB(0, 255, 0), "Test3D 0.1 by Jacob R. Lifshay (c) 2012");
        DrawOverlayText(0, 16, RGB(255, 255, 0), polycountbuf);
        drawfn = &drawpoly;
        for(i=0;i<polygoncount+1;i++)
        {
            if(i >= polygoncount)
            {
                p.x1 = -25.0;
                p.y1 = -8.8;
                p.z1 = -25.0;

                p.x2 =  25.0;
                p.y2 = -8.8;
                p.z2 = -25.0;

                p.x3 = -25.0;
                p.y3 = -8.8;
                p.z3 =  25.0;

                p.u1 = 0;
                p.v1 = 0;

                p.u2 = 1;
                p.v2 = 0;

                p.u3 = 0;
                p.v3 = 1;
                p.image = knoppiximg;
                drawfn = &drawplane;
            }
            else
            {
                p = polys[i];
                p.x1 *= scalefactor;
                p.y1 *= scalefactor;
                p.z1 *= scalefactor;
                p.x2 *= scalefactor;
                p.y2 *= scalefactor;
                p.z2 *= scalefactor;
                p.x3 *= scalefactor;
                p.y3 *= scalefactor;
                p.z3 *= scalefactor;
                //if(p.z1 > 6.3 && p.y1 < 30.0)
                {
                    x = p.x1 * cos(propang) - p.y1 * sin(propang);
                    y = p.y1 * cos(propang) + p.x1 * sin(propang);
                    p.x1 = x;
                    p.y1 = y;

                    x = p.x2 * cos(propang) - p.y2 * sin(propang);
                    y = p.y2 * cos(propang) + p.x2 * sin(propang);
                    p.x2 = x;
                    p.y2 = y;

                    x = p.x3 * cos(propang) - p.y3 * sin(propang);
                    y = p.y3 * cos(propang) + p.x3 * sin(propang);
                    p.x3 = x;
                    p.y3 = y;
                }

                x = p.x1;
                y = p.y1;
                z = p.z1;
                p.x1 = x;
                p.y1 = z + ytrans;
                p.z1 = y;

                x = p.x2;
                y = p.y2;
                z = p.z2;
                p.x2 = x;
                p.y2 = z + ytrans;
                p.z2 = y;

                x = p.x3;
                y = p.y3;
                z = p.z3;
                p.x3 = x;
                p.y3 = z + ytrans;
                p.z3 = y;
            }

            x = p.x1 * cos(xang) - p.z1 * sin(xang);
            z = p.z1 * cos(xang) + p.x1 * sin(xang);
            p.x1 = x;
            p.z1 = z;

            x = p.x2 * cos(xang) - p.z2 * sin(xang);
            z = p.z2 * cos(xang) + p.x2 * sin(xang);
            p.x2 = x;
            p.z2 = z;

            x = p.x3 * cos(xang) - p.z3 * sin(xang);
            z = p.z3 * cos(xang) + p.x3 * sin(xang);
            p.x3 = x;
            p.z3 = z;

            y = p.y1 * cos(zang) - p.z1 * sin(zang);
            z = p.z1 * cos(zang) + p.y1 * sin(zang);
            p.y1 = y;
            p.z1 = z;

            y = p.y2 * cos(zang) - p.z2 * sin(zang);
            z = p.z2 * cos(zang) + p.y2 * sin(zang);
            p.y2 = y;
            p.z2 = z;

            y = p.y3 * cos(zang) - p.z3 * sin(zang);
            z = p.z3 * cos(zang) + p.y3 * sin(zang);
            p.y3 = y;
            p.z3 = z;


            p.z1 += 40;
            p.z2 += 40;
            p.z3 += 40;

            if(i < polygoncount)
            {
                /*getabcd(&a, &b, &c, &d, p.x1, p.y1, p.z1, p.x2, p.y2, p.z2, p.x3, p.y3, p.z3);
                if(d == 0) d = 1;
                a /= d;
                b /= d;
                c /= d;
                Normalize(&a, &b, &c);
                p.image = &singlecolorimage[16 + (int)(15 * min(1.0, 0.3 + 0.7 * max((float)0.0, DotProduct(lx, ly, lz, a, b, c))))];*/

                p.image = earthimg;
            }

            (*drawfn)(&p);
        }
        xang += xanginc / fps;
        waveang += waveanginc / fps;
        zang = M_PI / 5 * (sin(waveang * 1.00123456) - 0.9);
        propang += propanginc / fps;
        update(RGB(64, 64, 255));
    }

    while(kbhit()) getch();
    freeImage(knoppiximg);
    freeImage(earthimg);
    term();
    return 0;
}

