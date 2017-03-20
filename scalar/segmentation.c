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
    struct Node * next;
    int row;
    int column;
};

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
    unsigned char *img_copy = (unsigned char*) malloc(img_info.ImageSize);
    unsigned char *img_mask = (unsigned char*) malloc(img_info.ImageSize);
    int g;
    for (g = 0; g < img_info.ImageSize; g++)
    {
        img_copy[g] = img[g];
        img_mask[g] = img[g];
    }

    // 3. Relate image areas by thresholding PDF of Markovian Gibbs Probability
    //    (Refer to: Image Prediction)
    /*
        These are some variable descriptions. 
        It'll be easier to understand if you read Bitmap Wikipedia.

        byte_depth  : how many bytes are per pixel.
        byte_padd   : how many bytes used to align the rows by 4 bytes
        byte_width  : how many bytes are per row INCLUDING the padding
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
    
    // 4. Produce Mask from Thresholded MRF
    printf("\nStarting Ours\n");
    //int X = img_info.Width;
    int midY = img_info.Width / 2 * byte_depth;
    int midX = img_info.Height /2;
    int rowLength = byte_width;
    unsigned char lum [byte_depth];
    //int i,j,k;
    for (i = 0; i < byte_depth; i++)
    {
        lum[i] = img_mask[midX * rowLength + midY + i];
    }
    //lum[0] = img_mask[midX * rowLength + midY];
    //lum[1] = img_mask[midX * rowLength + midY + 1];
    //lum[2] = img_mask[midY * rowLength + midY + 2];
    //printf("\nr = %u, g = %u, b = %u\n", lum[0], lum[1], lum[2]);
    struct Node * ToVisit = NULL; //linked list holding nodes to be visited
    ToVisit = malloc(sizeof(struct Node));
    struct Node * Visited = NULL; //linked list holding nodes that have been visited
    //Visited = malloc(sizeof(struct Node));
    //original array to be analyzed
    unsigned char *BFSArray = (unsigned char*) malloc(img_info.ImageSize);
    printf("\n %d \n", img_info.ImageSize);
    //int BFSArray[]; //destination array to put results
    ToVisit->next = NULL;
    ToVisit->row = midX; //set head of to visit list to center pixel
    ToVisit->column = midY;
    int leftX; //used to store the x and y values of four neighbors
    int leftY;
    int rightX;
    int rightY;
    int upX;
    int upY;
    int downX;
    int downY;
    int downCheck; //0 if not in visited list, 1 if in visited list
    int upCheck;
    int leftCheck;
    int rightCheck;
    
    //for (j = 0; j < img_info.ImageSize; j++)
    //{
    //    printf("%u ", img_mask[j]);
    //}
    for (j = 0; j < img_info.ImageSize; j++)
    {
        BFSArray[j] = 0;
    }
    long int count = 0;
    while (ToVisit != NULL)
    {
        
        int downCheck = 0; //0 if not in visited list, 1 if in visited list
        int upCheck = 0;
        int leftCheck = 0;
        int rightCheck = 0;
        if (count < 5)
        printf("\n row  = %d column = %d\n", ToVisit->row, ToVisit->column);
        leftX = ToVisit->row;
        leftY = ToVisit->column - byte_depth; //moving 3 values to left to get previous value of same color
        if(count < 5)
        printf("\nleftX = %d, leftY = %d\n", leftX, leftY);
        rightX = ToVisit->row;
        rightY = ToVisit->column + byte_depth;//moving 3 values to right to get next value of same color
        upX = ToVisit->row - 1; //move 1 row up to get top neighbor
        upY = ToVisit->column;
        downX = ToVisit->row + 1; //move 1 row down to get bottom neighbor
        downY = ToVisit->column;
        struct Node * current = NULL;
        //current = malloc(sizeof(struct Node));
        current = Visited;
        if (img_mask[ToVisit->row * rowLength + ToVisit->column]    == lum[0])
        {
                   while (current != NULL) //see if neighbor nodes have been visited
        {
            
            if ((leftX == current->row && leftY == current->column) || leftY < 0 )
            {
                leftCheck = 1;
                //printf("\n left y = %d \n", leftY);
            }
            if ((rightX == current->row && rightY == current->column) || rightY > img_info.Width *byte_depth)
            {
                rightCheck = 1;
            }
            if ((upX == current->row && upY == current->column) || upX < 0)
            {
                upCheck = 1;
            }
            if ((downX == current->row && downY == current->column) || downX > img_info.Height)
            {
                downCheck = 1;
            }
            
            current = current->next;
        }
            
        
        current = ToVisit;
            
            while (current != NULL) //see if neighbor nodes have been visited
            {
                
                if ((leftX == current->row && leftY == current->column) )
                {
                    leftCheck = 1;
                    //printf("\n left y = %d \n", leftY);
                }
                if ((rightX == current->row && rightY == current->column))
                {
                    rightCheck = 1;
                }
                if ((upX == current->row && upY == current->column))
                {
                    upCheck = 1;
                }
                if ((downX == current->row && downY == current->column))
                {
                    downCheck = 1;
                }
                
                current = current->next;
            }
            current = ToVisit;
        
        //printf("\nafter to visit\n");
        while( current != NULL && current->next != NULL)
        {
            current = current->next; //find last node
            //printf("\n in the while\n");
        }
        //printf("\nafter to while\n");
        //Visited = malloc(sizeof(struct Node));
        if (leftCheck == 0) //if not visited, add to to visit
        {
            
            struct Node * newOne = NULL;
            newOne = malloc(sizeof(struct Node));
            newOne->row = leftX;
            newOne->column = leftY;
            newOne->next = NULL;
            current->next= newOne;
            current = current->next;
            
        }
        //printf("\nafter to first if\n");
        if (rightCheck == 0)
        {
            
            struct Node * newOne = NULL;
            newOne = malloc(sizeof(struct Node));
            newOne->row = rightX;
            newOne->column = rightY;
            newOne->next = NULL;
            current->next= newOne;
            current = current->next;

        }
        //printf("\nafter to secind if\n");
        if (upCheck == 0)
        {
            
            struct Node * newOne = NULL;
            newOne = malloc(sizeof(struct Node));
            newOne->row = upX;
            newOne->column = upY;
            newOne->next = NULL;
            current->next= newOne;
            current = current->next;

        }
        if (downCheck == 0)
        {
            
            struct Node * newOne = NULL;
            newOne = malloc(sizeof(struct Node));
            newOne->row = downX;
            newOne->column = downY;
            newOne->next = NULL;
            current->next= newOne;
            current = current->next;

        }
        }
        if (byte_depth == 3)
        {
            if (img_mask[ToVisit->row * rowLength + ToVisit->column]    == lum[0] &&
                img_mask[ToVisit->row * rowLength + ToVisit->column +1] == lum[1] &&
                img_mask[ToVisit->row * rowLength + ToVisit->column +2] == lum[2])
            {
                BFSArray[ToVisit->row * rowLength + ToVisit->column]    = 1;
                BFSArray[ToVisit->row * rowLength + ToVisit->column +1] = 1;
                BFSArray[ToVisit->row * rowLength + ToVisit->column +2] = 1;
            } //if all three values match, set corresponding index in final matrix to 1
            current = Visited;
            while (current != NULL && current->next != NULL)
            {
                current = current->next;
            }
            struct Node * newNode = NULL;
            newNode = malloc(sizeof(struct Node));
            newNode->row = ToVisit->row;
            newNode->column = ToVisit->column;
            newNode->next = NULL;
            if (current == NULL)
            {
                Visited = newNode;
            }
            else
            {
                current->next = newNode;
            }
            
        }
        if (byte_depth == 1)
        {
            

            
            if (img_mask[ToVisit->row * rowLength + ToVisit->column]    == lum[0])
            {
                
                
                BFSArray[ToVisit->row * rowLength + ToVisit->column]    = 1;
                
            }
            
            current = Visited;
            while (current != NULL && current->next != NULL)
            {
            current = current->next;
            }
            
            struct Node * newNode = NULL;
            newNode = malloc(sizeof(struct Node));
            
            newNode->row = ToVisit->row;
            
            newNode->column = ToVisit->column;
            newNode->next = NULL;
            
            if (current == NULL)
            {
                Visited = newNode;
            }
            else
            {
                current->next = newNode;
            }
            
        }
        
        if (byte_depth == 2)
        {
            if (img_mask[ToVisit->row * rowLength + ToVisit->column]    == lum[0] &&
                img_mask[ToVisit->row * rowLength + ToVisit->column +1] == lum[1])
            {
                BFSArray[ToVisit->row * rowLength + ToVisit->column]    = 1;
                BFSArray[ToVisit->row * rowLength + ToVisit->column +1] = 1;
            }
            current = Visited;
            while (current != NULL && current->next != NULL)
            {
            current = current->next;
            }
            struct Node * newNode = NULL;
            newNode = malloc(sizeof(struct Node));
            newNode->row = ToVisit->row;
            newNode->column = ToVisit->column;
            newNode->next = NULL;
            if (current == NULL)
            {
                Visited = newNode;
            }
            else
            {
                current->next = newNode;
            }
        }
       
            
            printf("%d\n", count);
        
        
        count++;
        struct Node * nextOne;
        
        nextOne = ToVisit->next;
        free(ToVisit);
        ToVisit = nextOne;
        
        
    }
    printf("\nEND\n");
    //for (j = 0; j < img_info.ImageSize; j++)
    //{
    //    printf("%u ", BFSArray[j]);
    //}

    // 5. Apply mask to image
    for (g = 0; g < img_info.ImageSize; g++)
        img[g] *= BFSArray[g];
    
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
    status = overwrite_bitmap(img_mask_name, &BFSArray);
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