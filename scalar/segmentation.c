#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

// Higher temperature smoothens the image more. It's like a pre-filter.
// It will make more likely for pixels within object boundary to be grouped.
#define TEMPERATURE 23

// The more you iterate through CDF thresholding, the clearer the boundaries are.
// Eventually it will converge and yield diminishing returns.
#define ITERATIONS 3 // is Just enough

// A threshold divides which pixels are assigned to which group.
// A good threshold perfectly partitions the object from the background.
#define THRESHOLD 0.85

// Higher order MRF makes it easier to tell if pixel is near object or not.
// Image dimension is divided by PARTITION to determine the MRF order.
// Higher PARTITION means lower MRF ORDER. (PARTITION = SIZE/ORDER)
#define PARTITION 80

#pragma pack(push, 1)
typedef struct
{
    short int confirm_bmp;
    int bfSize;
    short int bfReserved1;
    short int bfReserved2;
    int bfOffBits;
} BMPFILEHEADER;
typedef struct
{
    int biSize;
    int biWidth;
    int biHeight;
    short int biPlanes;
    short int bibitPerPix;
    int biCompression;
    int biImageSize;
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
    char          *img_mask_name = "image_mask.bmp";
    BMPINFOHEADER  img_info;
    unsigned char *img;
    int status = load_bitmap(img_name, &img_info, &img);
    if (status == -1) {
        printf("ERROR: 1. File DNE\n");
        return 0;
    } else if (status == -2){
        printf("ERROR: 2. Could not allocate memory\n");
        return 0;
    } else if (status != 0) {
        printf("ERROR: 3. Only read %d bytes\n", status);
        return 0;
    }

    // 2. Copy the original image in a separate buffer and leave the original untouched.
    unsigned char *img_copy = (unsigned char*) malloc(img_info.biImageSize);
    unsigned char *img_mask = (unsigned char*) malloc(img_info.biImageSize);
    int g;
    for (g = 0; g < img_info.biImageSize; g++)
    {
        img_copy[g] = img[g];
        img_mask[g] = img[g];
    }

    // 3. Relate image areas by thresholding PDF of Markovian Gibbs Probability
    //    (Refer to: Image Prediction)
    int byte_depth  = img_info.bibitPerPix / 8;
    int byte_padd   = (4 - img_info.biWidth * byte_depth & 0x3) & 0x3;
    int byte_width  = img_info.biWidth * byte_depth + byte_padd;
    int byte_offset = (img_info.biWidth < img_info.biHeight) ? img_info.biWidth / PARTITION : img_info.biHeight / PARTITION;
    int h, i, j, k;
    for (h = 0; h < ITERATIONS; h++)
    {
        //  In the beginning of every iteration:
        //      img_copy is the one that was iterated,
        //      img_mask is the result of iteration.
        //  Data always flows from img_copy -> img_mask.
        //  Switch these two to modify the content of img_mask again.
        unsigned char *img_to_modify = img_mask;
        img_mask = img_copy;
        img_copy = img_to_modify;

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
                    for (lum = 0; lum <= 255; lum++) ///////////////// Optimizable
                    {
                        int l, m, Eq = 0, order = byte_offset*byte_offset; /////////// Optimizable
                        for (l = -byte_offset; l <= byte_offset; l++)
                            for (m = -byte_offset; m <= byte_offset; m++)
                                if (l*l + m*m <= order)
                                    Eq += (img_copy[(i+l)*byte_width + (j+m)*byte_depth + k] != (unsigned char) lum) ? 5 : 0;
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
        printf("Iteration %d done.\n", h+1);
    }

    // 4. Boogey segmentation code ///////////////////// Optimizable
    for (g = 0; g < img_info.biImageSize; g++)
        img[g] = (img_mask[g] == img_mask[img_info.biImageSize/2]) ? img[g] : 0;

    //  6. Save segmented image
    status = overwrite_bitmap(img_name, &img);
    if (status == -1) {
        printf("ERROR: 4. Could not open file\n");
        return 0;
    } else if (status != 0) {
        printf("ERROR: 5. Only wrote %d bytes\n", status);
        return 0;
    }

    //  7. Save image mask
    status = overwrite_bitmap(img_mask_name, &img_mask);
    if (status == -1) {
        printf("ERROR: 6. Could not open file\n");
        return 0;
    } else if (status != 0) {
        printf("ERROR: 7. Only wrote %d bytes\n", status);
        return 0;
    }

    free(img);
    free(img_mask);
    return 0;
}