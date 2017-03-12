#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

// Higher temperature smoothens the image more. It's like a pre-filter.
#define TEMPERATURE 15

// The more you iterate through CDF thresholding, the clearer the boundaries are.
#define ITERATIONS 30

// Higher threshold groups together more pixels together
// The resulting image turns out to be brighter, but also more two-tone.
#define THRESHOLD 0.3

// Higher order MRF patches together clarifies the boundary more per iteration.
// However, it could also blur the object boundary on noisy .
#define SECOND_ORDER
#define THIRD_ORDER

#pragma pack(push, 1)
typedef struct
{
    short int confirm_bmp;  // File type indicator. Must be 0x4d42 for .bmp.
    int bfSize;             // Specifies the size in bytes of the bmp file
    short int bfReserved1;  // Reserved; must be 0
    short int bfReserved2;  // Reserved; must be 0
    int bfOffBits;          // Specifies the offset in bytes from the bmpfileheader to the bmp bits
} BMPFILEHEADER;
typedef struct
{
    unsigned int biSize;          // Specifies the number of bytes required by the struct
    int biWidth;          // Specifies width in pixels
    int biHeight;         // Species height in pixels
    unsigned short int biPlanes;         // Specifies the number of color planes, must be 1
    unsigned short int bibitPerPix;       // Specifies the number of bit per pixel
    unsigned int biCompression;   // Spcifies the type of compression
    unsigned int biImageSize;     // Size of image in bytes
    int biXPelsPerMeter;  // Number of pixels per meter in x axis
    int biYPelsPerMeter;  // Number of pixels per meter in y axis
    unsigned int biClrUsed;       // Number of colors used by the bmp
    unsigned int biClrImportant;  // Number of colors that are important
    unsigned int redChannelBitmask;
    unsigned int greenChannelBitmask;
    unsigned int blueChannelBitmask;
    unsigned int AlphaChannelBitmask;
    unsigned int ColorSpaceType;
    int ColorSpaceEndpoints;
    unsigned int gammaChannelRed;
    unsigned int gammaChannelGreen;
    unsigned int gammaChannelBlue;
} BMPINFOHEADER;
#pragma pack(pop)

int load_bitmap(char *filename, BMPINFOHEADER *bmpInfoHeader, unsigned char **img)
{
    //  1. Open filename in read binary mode
    FILE *filePtr = fopen(filename,"rb");
    if (filePtr == NULL) return -1;

    //  2. Read the headers
    BMPFILEHEADER bmpFileHeader;
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, filePtr);
    fread(bmpInfoHeader, sizeof(BMPINFOHEADER), 1, filePtr);

    //  3. malloc for bitmap
    int imgSize = bmpInfoHeader->biImageSize;
    *img = (unsigned char*) malloc(imgSize*sizeof(unsigned char));
    if (*img == NULL) return -2;

    //  4. Read the bitmap
    fseek(filePtr, bmpFileHeader.bfOffBits, SEEK_SET);
    int count = fread(*img, sizeof(unsigned char), imgSize, filePtr);
    if (count == 0) printf("ERROR: Could not read anything\n");
    if (count != imgSize) return count;
    fclose(filePtr);

    return 0;
}

int overwrite_bitmap(char *filename, unsigned char **img)
{
    //  1. Open filename in "R/W binary at beginning" mode
    FILE *filePtr = fopen(filename, "rb+");
    if (filePtr == NULL) return -1;

    //  2. Read file header to find where and how much to write image
    BMPFILEHEADER bmpFileHeader;
    BMPINFOHEADER bmpInfoHeader;
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, filePtr);
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, filePtr);

    //  3. Write all file info
    fseek(filePtr, bmpFileHeader.bfOffBits, SEEK_SET);
    int imgSize = bmpInfoHeader.biImageSize;
    int count = fwrite(*img, sizeof(unsigned char), imgSize, filePtr);
    if (count == 0) printf("ERROR: Could not write anything\n");
    if (count != imgSize) return count;
    fclose(filePtr);

    return 0;
}

int main(){

    //  1. Load bitmap
    char          *img_name = "image.bmp";
    BMPINFOHEADER  img_info;
    unsigned char *img;
    int status = load_bitmap(img_name, &img_info, &img);
    if (status == -1) {
        printf("ERROR: File DNE\n");
        return 0;
    } else if (status == -2){
        printf("ERROR: Could not allocate memory\n");
        return 0;
    } else if (status != 0) {
        printf("ERROR: Only read %d bytes\n", status);
        return 0;
    }

    // 2. Relate image areas by thresholding PDF of Markovian Gibbs Probability
    //    (Refer to: Image Prediction)
    int h, i, j, k;
    unsigned char *img_mask = (unsigned char*) malloc(img_info.biImageSize);
    int byte_depth  = img_info.bibitPerPix / 8;
    int byte_padd   = (4 - img_info.biWidth * byte_depth % 4) % 4;
    int byte_width  = img_info.biWidth * byte_depth + byte_padd;
    int byte_offset = 1;
        #ifdef SECOND_ORDER
        byte_offset = 2;
        #endif
        #ifdef THIRD_ORDER
        byte_offset = 3;
        #endif
    for (h = 0; h < ITERATIONS; h++)
        for (i = byte_offset; i < img_info.biHeight-byte_offset; i++)
            for (j = byte_offset; j < img_info.biWidth-byte_offset; j++)
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
                        int Eq  = (img[(i-1)*byte_width + j*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+1)*byte_width + j*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[i*byte_width + (j-1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[i*byte_width + (j+1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            #ifdef SECOND_ORDER
                            Eq += (img[(i-1)*byte_width + (j-1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i-1)*byte_width + (j+1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+1)*byte_width + (j-1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+1)*byte_width + (j+1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i-2)*byte_width + j*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+2)*byte_width + j*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[i*byte_width + (j-2)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[i*byte_width + (j+2)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            #endif
                            #ifdef THIRD_ORDER
                            Eq += (img[(i-2)*byte_width + (j+1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i-2)*byte_width + (j-1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+2)*byte_width + (j+1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+2)*byte_width + (j-1)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+1)*byte_width + (j-2)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i-1)*byte_width + (j-2)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+1)*byte_width + (j+2)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i-1)*byte_width + (j+2)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i-3)*byte_width + j*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[(i+3)*byte_width + j*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[i*byte_width + (j-3)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            Eq += (img[i*byte_width + (j+3)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
                            #endif
                        gibbs_CDF[lum+1] = gibbs_CDF[lum] + exp(-Eq/TEMPERATURE);
                    }

                    // Threshold CDF
                    for (lum = 0; lum <= 255; lum++)
                        if (gibbs_CDF[lum+1]/gibbs_CDF[256] > THRESHOLD)
                        {
                            img_mask[i*byte_width+j*byte_depth+k] = (unsigned char) lum;
                            lum = 256;
                        }
                }

    //  5. Save bitmap
    status = overwrite_bitmap(img_name, &img_mask);
    if (status == -1) {
        printf("ERROR: Could not open file\n");
        return 0;
    } else if (status != 0) {
        printf("ERROR: Only wrote %d bytes\n", status);
        return 0;
    }

    free(img);
    free(img_mask);
    return 0;
}