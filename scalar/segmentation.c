#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

// Higher temperature smoothens the image more. It's like a pre-filter.
// It will make more likely for pixels within object boundary to be grouped.
#define TEMPERATURE 40

// The more you iterate through CDF thresholding, the clearer the boundaries are.
// Eventually it will converge and yield diminishing returns.
#define ITERATIONS 1 // is usually Just enough

// A threshold divides which pixels are assigned to which group.
// A good threshold perfectly partitions the object from the background.
#define THRESHOLD 0.9

// Higher order MRF makes it easier to tell if pixel is near object or not.
// Image dimension is divided by PARTITION to determine the MRF order.
// Higher PARTITION means lower MRF ORDER. (PARTITION = SIZE/ORDER)
#define PARTITION 120

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
    int Size;
    int Width;
    int Height;
    short int Planes;
    short int bitPerPix;
    int Compression;
    int ImageSize;
} BMPINFOHEADER;
#pragma pack(pop)

struct Node //struct used for linked lists
{
    int row;
    int column;
    struct Node *next;
};

struct timespec diff(struct timespec start, struct timespec end)
{
  struct timespec temp;
  if ((end.tv_nsec-start.tv_nsec)<0) {
    temp.tv_sec = end.tv_sec-start.tv_sec-1;
    temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
  } else {
    temp.tv_sec = end.tv_sec-start.tv_sec;
    temp.tv_nsec = end.tv_nsec-start.tv_nsec;
  }
  return temp;
}

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
    int imgSize = bmpInfoHeader->ImageSize;
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
    int imgSize = bmpInfoHeader.ImageSize;
    int count = fwrite(*img, sizeof(unsigned char), imgSize, filePtr);
    if (count == 0) printf("ERROR: Could not write anything\n");
    if (count != imgSize) return count;
    fclose(filePtr);

    return 0;
}

int main(){

    //  0. Initialize timestamp calculator
    struct timespec time1, time2, result;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);

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

    //  2. Copy the original image in a separate buffer and leave the original untouched.
    unsigned char *img_copy = (unsigned char*) malloc(img_info.ImageSize);
    unsigned char *img_mask = (unsigned char*) malloc(img_info.ImageSize);
    int g;
    for (g = 0; g < img_info.ImageSize; g++)
        img_mask[g] = img[g];

    //  3. Relate image areas by thresholding PDF of Markovian Gibbs Probability
    //    (Refer to: Image Prediction)
    /*
        These are some variable descriptions. 
        It'll be easier to understand if you read Bitmap Wikipedia.

        byte_depth  : how many bytes are per pixel.
        byte_padd   : how many bytes used to align the xs by 4 bytes
        byte_width  : how many bytes are per x INCLUDING the padding
        byte_offset : the radius that bounds what pixels are considered neighbors in MRF
    */
    int byte_depth  = img_info.bitPerPix / 8;
    int byte_padd   = (4 - img_info.Width * byte_depth & 0x3) & 0x3;
    int byte_width  = img_info.Width * byte_depth + byte_padd;
    int byte_offset = (img_info.Width < img_info.Height) ? 
                        img_info.Width/PARTITION : img_info.Height/PARTITION;
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

        for (i = byte_offset; i < img_info.Height-byte_offset; i++)
            for (j = byte_offset; j < img_info.Width-byte_offset; j++)
                for (k = 0; k < byte_depth; k++)
                {
                    // Initialize Gibbs CDF array
                    double gibbs_CDF[257];      // Gibbs PDF for this pixel (i,j)'s
                                                // luminance to be 0,1...255 or lower 
                                                // based on Markovian neighbor values.
                           gibbs_CDF[0] = 0;    // The first element is for making CDF 
                                                // generation easier by having an index 0
                                                // for lum -1, whose gibbs_PDF is 0.
                    int lum, order = byte_offset*byte_offset;
                    for (lum = 0; lum <= 255; lum++) ///////////////// Optimizable
                        gibbs_CDF[lum+1] = order << 2;

                    // Calculate Equipotential of each luminance using Markovian Neighbors
                    int l, m;
                    for (l = -byte_offset; l <= byte_offset; l++)
                        for (m = -byte_offset; m <= byte_offset; m++)
                        {
                            int lum = img_copy[(i+l)*byte_width + (j+m)*byte_depth + k] + 1;
                            gibbs_CDF[lum] -= (l*l + m*m <= order) ? 5 : 0;
                        }

                    // Generate CDF
                    for (lum = 0; lum <= 255; lum++)
                        gibbs_CDF[lum+1] = gibbs_CDF[lum] + exp(-gibbs_CDF[lum+1]/TEMPERATURE);

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

    //  4. Save thresholded MRF image
    status = overwrite_bitmap(img_mask_name, &img_mask);
    if (status == -1) {
        printf("ERROR: 6. Could not open file\n");
        return 0;
    } else if (status != 0) {
        printf("ERROR: 7. Only wrote %d bytes\n", status);
        return 0;
    }
    
    //  5. Produce Mask from Thresholded MRF using BFS
    int midX = img_info.Height / 2;
    int midY = img_info.Width  / 2;
    unsigned char *center   = &img_mask[midX * byte_width + midY * byte_depth];
    unsigned char *BFSArray = (unsigned char*) calloc(img_info.ImageSize, 1);
    struct Node   *visiting = (struct Node*) malloc(sizeof(struct Node));
          visiting->row     = midX;
          visiting->column  = midY;
          visiting->next    = NULL;
    struct Node *last_in_queue  = visiting;
    while (visiting != NULL) 
    {
        //  Check all 4 vertical and horizontal neighbors.
        int past_col=-1, col=-1, row=0;
        for (i=0; i<4; i++, past_col=col, col=row*-1, row=past_col)
        {
            //  Index of the neighbor
            int x = visiting->row+row;
            int y = visiting->column+col;

            //  Tests
            // 1. The visiting struct Node is always "valid (has 1 same RGB as center)"
            // 2. If neighbor is not valid, move on. If "valid" and unvisited, mark valid and add to queue
            // 3. On mask, 0 is unvisited, 1 is valid
            if ((x|y) < 0 || x >= img_info.Height || y >= img_info.Width) //  Boundary check
                continue;
            if (BFSArray[x*byte_width + y*byte_depth]) //  If already marked valid, don't check again
                continue;
            for (j = 0; j < byte_depth; j++)
                j = (img_mask[x*byte_width + y*byte_depth + j] == center[j]) ? byte_depth+1 : j;
            if (j < byte_depth+1)
                continue;

            //  The pixel is valid. Mark as valid and add to queue.
            for (j = 0; j < byte_depth; j++)
                BFSArray[x*byte_width + y*byte_depth + j] = 1;

            last_in_queue->next = (struct Node*) malloc(sizeof(struct Node));
            last_in_queue       = last_in_queue->next;

            last_in_queue->row    = x;
            last_in_queue->column = y;
            last_in_queue->next   = NULL;
        }
       
        struct Node *to_visit = visiting->next;
        free(visiting);
        visiting = to_visit; //free struct Node and move on to the next on the list
    }

    //  6. Apply mask to image
    for (g = 0; g < img_info.ImageSize; g++)
        img[g] *= BFSArray[g];

    //  7. Calculate code duration
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time2);
    result = diff(time1, time2);
    long int code_duration = 1000000000 * result.tv_sec + result.tv_nsec;
    printf("\n::: Duration: %ldns\n\n", code_duration);
    
    //  8. Save segmented image
    status = overwrite_bitmap(img_name, &img);
    if (status == -1) {
        printf("ERROR: 4. Could not open file\n");
        return 0;
    } else if (status != 0) {
        printf("ERROR: 5. Only wrote %d bytes\n", status);
        return 0;
    }

    free(img);
    free(img_mask);
    free(img_copy);
    free(BFSArray);
    return 0;
}
