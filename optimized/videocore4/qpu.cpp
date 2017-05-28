#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include "QPULib.h"

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

void is_neighbor(Ptr<Int> hl, Ptr<Int> hm, Ptr<Int> hord, Ptr<Int> hr)
{
	Int dl   = *hl;
	Int dm   = *hm;
	Int dord = *hord;
	Int dr;
	Where (dl*dl + dm*dm <= dord)
		dr = dord;
	End
	*hr = dr;
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
    int R2 = byte_offset*byte_offset;
    int D  = 2*byte_offset + 1;
    int h, i, j, k, l, m;
    
    struct timespec time1, time2, result;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);
    
    /*char is_neighbor[D*D];
    for (l = -byte_offset; l <= byte_offset; l++)
		for (m = -byte_offset; m <= byte_offset; m++)
			is_neighbor[(l+byte_offset)*D+m+byte_offset] = (l*l + m*m <= R2);*/
	SharedArray<int> hl(D), hm(D), hord(D), hr(D);
	auto k_eq = compile(is_neighbor);
	k_eq.setNumQPUs(8);
                    // Calculate Equipotential of each luminance using Markovian Neighbors
                    for (l = -byte_offset; l <= byte_offset; l+=1){
						for (int n = 0; n < D; n++)
						{
							hl[n]   = l;
							hm[n]   = n - byte_offset;
							hord[n] = R2;
							hr[n]   = 0;
						}
						k_eq(&hl, &hm, &hord, &hr);
					}/*
                    for (l = -byte_offset; l <= byte_offset; l+=1){
						int n = D;
                        for (m = -byte_offset; m <= byte_offset; m+=1)
                            n -= (is_neighbor[(l+byte_offset)*D+m+byte_offset]) ? 5 : 0;
					}*/
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time2);
    result = diff(time1, time2);
    long int code_duration = 1000000000 * result.tv_sec + result.tv_nsec;
    printf("\n::: Duration: %ldns\n\n", code_duration);
						

    free(img);
    free(img_mask);
    free(img_copy);
    return 0;
}
