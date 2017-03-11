#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define TEMPERATURE 1
#define THRESHOLD 0.7
#define SECOND_ORDER

#pragma pack(push, 1)
typedef struct
{
    short int confirm_bmp;  // File type indicator. Must be 0x4d42 for .bmp.
    int bfSize;             // Specifies the size in bytes of the bmp file
    short int bfReserved1;  // Reserved; must be 0
    short int bfReserved2;  // Reserved; must be 0
    int bfOffBits;          // Specifies the offset in bytes from the bmpfileheader to the bmp bits
} BMPFILEHEADER;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
    int biSize;          // Specifies the number of bytes required by the struct
    int biWidth;          // Specifies width in pixels
    int biHeight;         // Species height in pixels
    short int biPlanes;         // Specifies the number of color planes, must be 1
    short int biBitCount;       // Specifies the number of bit per pixel
    int biCompression;   // Spcifies the type of compression
    int biSizeImage;     // Size of image in bytes
    int biXPelsPerMeter;  // Number of pixels per meter in x axis
    int biYPelsPerMeter;  // Number of pixels per meter in y axis
    int biClrUsed;       // Number of colors used by the bmp
    int biClrImportant;  // Number of colors that are important
} BMPINFOHEADER;
#pragma pack(pop)

int load_bitmap(char *filename, BMPINFOHEADER *bmpInfoHeader, unsigned char *img)
{
    //  1. Open filename in read binary mode
    FILE *filePtr = fopen(filename,"rb");
    if (filePtr == NULL)
        return -1;

    //  2. Read the headers
    BMPFILEHEADER bmpFileHeader;
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, filePtr);
    fread(bmpInfoHeader, sizeof(BMPINFOHEADER), 1, filePtr);

    //  3. malloc for bitmap
    int imgSize = bmpInfoHeader->biSizeImage;
    img = (unsigned char*) malloc(imgSize);
    if (img == NULL)
    {
        free(img); fclose(filePtr); return -2;
    }

    //  4. Read the bitmap
    fseek(filePtr, bmpFileHeader.bfOffBits, SEEK_SET);
    fread(img, imgSize, 1, filePtr);
    fclose(filePtr);

    return 0;
}

int overwrite_bitmap(char *filename, unsigned char *img)
{
    //  1. Open filename in "R/W binary at beginning" mode
    FILE *filePtr = fopen(filename, "rb+");
    if (filePtr == NULL)
        return -1;

    //  2. Read file header to find where and how much to write image
    BMPFILEHEADER bmpFileHeader;
    BMPINFOHEADER bmpInfoHeader;
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, filePtr);
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, filePtr);

    //  3. Write all file info
    fseek(filePtr, bmpFileHeader.bfOffBits, SEEK_SET);
    fwrite(img, bmpInfoHeader.biSizeImage, 1, filePtr);
    fclose(filePtr);

    return 0;
}

int main(){

    //  1. Load bitmap
    char          *img_name = "image.bmp";
    BMPINFOHEADER  img_info;
    unsigned char *img;
    switch (load_bitmap(img_name, &img_info, img))
    {
        case -1: printf("ERROR: File DNE\n");
                 return 0;
        case -2: printf("ERROR: Out of memory\n");
                 return 0;
    }

    //  2. Relate image areas by thresholding PDF of Markovian Gibbs Probability
    //     (Refer to: Image Prediction)
    unsigned char img_mask[img_info.biSizeImage];
    int i, j, k;
    int byte_depth = img_info.biBitCount / 8;
    int byte_width = img_info.biWidth * byte_depth;
    for (i = 1; i < img_info.biHeight; i++)
        for (j = 1; j < img_info.biWidth; j++)
            for (k = 0; k < byte_depth; k++)
            {
                // Generate Gibbs CDF using Markovian Neighbors
                double gibbs_CDF[257];      // Gibbs PDF for this pixel (i,j)'s
                                            // luminance to be 0,1...255 or lower 
                                            // based on Markovian neighbor values.
                       gibbs_CDF[0] = 0;    // The first element is for making CDF 
                                            // generation easier by having an index 0
                                            // for lum -1, whose gibbs_PDF is 0.
                int lum;
                for (lum = 0; lum <= 255; lum++)
                {
                    int Eq  = (img[(i-1)*byte_width + j*byte_depth + k] != lum) ? 5 : 0;
                        Eq += (img[(i+1)*byte_width + j*byte_depth + k] != lum) ? 5 : 0;
                        Eq += (img[i*byte_width + (j-1)*byte_depth + k] != lum) ? 5 : 0;
                        Eq += (img[i*byte_width + (j+1)*byte_depth + k] != lum) ? 5 : 0;
                        #ifdef SECOND_ORDER
                        Eq += (img[(i-1)*byte_width + (j-1)*byte_depth + k] != lum) ? 5 : 0;
                        Eq += (img[(i-1)*byte_width + (j+1)*byte_depth + k] != lum) ? 5 : 0;
                        Eq += (img[(i+1)*byte_width + (j-1)*byte_depth + k] != lum) ? 5 : 0;
                        Eq += (img[(i+1)*byte_width + (j+1)*byte_depth + k] != lum) ? 5 : 0;
                        #endif

                    gibbs_CDF[lum+1] = gibbs_CDF[lum] + exp((double) (-Eq / TEMPERATURE));
                }

                // Threshold CDF
                for (lum = 0; lum <= 255; lum++)
                    if (gibbs_CDF[lum+1]/gibbs_CDF[256] > THRESHOLD)
                    {
                        printf("pixel (%d,%d,%d) is %d\n", i,j,k,lum);
                        img_mask[i*byte_width+j*byte_depth+k] = (unsigned char) lum;
                        lum = 256;
                    }
            }

    //  5. Save bitmap
    if (overwrite_bitmap(img_name, img_mask))
    {
        printf("ERROR: File I/O error\n");
        return 0;
    }

    free(img);
    return 0;
}