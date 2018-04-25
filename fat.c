#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

#define MAXCHAR 250
#define CHARCOMMAND 50
#define TRUE 1
#define FALSE 0
typedef int bool;

#define Partition_LBA_Begin 0 //first byte of the partition
#define ENTRIES_PER_SECTOR 16
#define DIR_SIZE 32 //in bytes
#define SECTOR_SIZE 64//in bytes
#define FAT_ENTRY_SIZE 4//in bytes
#define MAX_FILENAME_SIZE 8
#define MAX_EXTENTION_SIZE 3

//open file codes
#define MODE_READ 0
#define MODE_WRITE 1
#define MODE_BOTH 2
#define MODE_UNKNOWN 3 //when first created and directories

const uint8_t ATTR_READ_ONLY = 0x01;
const uint8_t ATTR_HIDDEN = 0x02;
const uint8_t ATTR_SYSTEM = 0x04;
const uint8_t ATTR_VOLUME_ID = 0x08;
const uint8_t ATTR_DIRECTORY = 0x10;
const uint8_t ATTR_ARCHIVE = 0x20;
const uint8_t ATTR_LONG_NAME = 0x0F;



uint32_t FAT_FREE_CLUSTER = 0x00000000;
uint32_t FAT_EOC = 0x0FFFFFF8;
uint32_t MASK_IGNORE_MSB = 0x0FFFFFFF;


//uint32_t FAT_EOC = 0x0FFFFFF8;

typedef struct 
{
    uint8_t filename[9];
    uint8_t extention[4];
    char parent[100];
    uint32_t firstCluster;
    int mode;
    uint32_t size;
    bool dir; //is it a directory
    bool isOpen;
    uint8_t fullFilename[13];
} __attribute((packed)) FILEDESCRIPTOR;

/*
 * Arrays storing our current position in the file
 * for clusters 0 represents first invalid element
 * for PATH NULL represents first invalid element
 * PATH_INDEX represents the index of last element in both tables
 */
uint32_t CLUSTER_PATH[128];  //stores cluster path for easy .. exec
unsigned int PATH_INDEX;
uint32_t current_cluster;


typedef struct 
{
	unsigned char jmp[3];
	char oem[8];
	unsigned short sector_size;
	unsigned char sectors_per_cluster;
	unsigned short reserved_sectors;
	unsigned char number_of_fats;
	unsigned short root_dir_entries;
	unsigned short total_sectors_short; // if zero, later field is used
	unsigned char media_descriptor;
	unsigned short fat_size_sectors;
	unsigned short sectors_per_track;
	unsigned short number_of_heads;
	unsigned int hidden_sectors;
	unsigned int total_sectors_long;

	unsigned int bpb_FATz32;
	unsigned short bpb_extflags;
	unsigned short bpb_fsver;
	unsigned int bpb_rootcluster;
	char volume_label[11];
	char fs_type[8];
	char boot_code[436];
	unsigned short boot_sector_signature;
}__attribute((packed)) FAT32BootBlock;

//FAT32DirectoryBlock;
//struct DirectoryEntry 
typedef struct
{
		uint8_t Name[11];       /* byes 0-10;  short name */
		uint8_t Attr;           /* byte 11;  Set to one of the File Attributes defined in FileSystem.h file */  
		uint8_t NTRes;          /* byte 12; Set value to 0 when a file is created and never modify or look at it after that. */
		uint8_t CrtTimeTenth;   /* byte 13; 0 - 199, timestamp at file creating time; count of tenths of a sec. */
		uint16_t CrtTime;       /* byte 14-15;  Time file was created */
		uint16_t CrtDate;       /* byte 16-17; Date file was created */
		uint16_t LstAccDate;    /* byte 18-19; Last Access Date. Date of last read or write. */
		uint16_t FstClusHI;     /* byte 20-21; High word of */
		uint16_t WrtTime;       /* byte 22-23; Time of last write. File creation is considered a write */
		uint16_t WrtDate;       /* byte 24-25; Date of last write. Creation is considered a write.  */
		uint16_t FstClusLO;     /* byte 26-27;  Low word of this entry's first cluster number.  */
		uint32_t FileSize;      /* byte 28 - 31; 32bit DWORD holding this file's size in bytes */
}__attribute((packed)) DirectoryEntry;


/*These need to be redone, basically copied to get working prototype */// //////////////////////////////////////////////////
int sectorsInDataRegion(FAT32BootBlock* bs);
int countOfClusters(FAT32BootBlock* bs);
uint32_t FAT_findFirstFreeCluster(char* fat_image, FAT32BootBlock* bs);
int FAT_writeFatEntry(char* fat_image, FAT32BootBlock* bs, uint32_t destinationCluster, uint32_t * newFatVal);
int createEntry(DirectoryEntry * entry,
			const char * filename, 
			const char * ext,
			int isDir,
			uint32_t firstCluster,
			uint32_t filesize );
uint32_t getLastClusterInChain(char* fat_image,FAT32BootBlock * bs, uint32_t firstClusterVal);
uint32_t FAT_extendClusterChain(char* fat_image, FAT32BootBlock* bs,  uint32_t clusterChainMember);
//uint32_t FAT_findNextOpenEntry(FAT32BootBlock* bs, uint32_t pwdCluster);
uint32_t getFileSizeInClusters(char* fat_image,FAT32BootBlock* bs, uint32_t firstClusterVal);
uint32_t FAT_findNextOpenEntry(char* fat_image,FAT32BootBlock* bs, uint32_t pwdCluster);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* ************************************************************************************************
 *
 * 			HELPERS FUNCTIONS
 *
 ************************************************************************************************ */

uint32_t root_dir_sector_count(FAT32BootBlock* bpb)
{
	return ((bpb->root_dir_entries * 32) + (bpb->sector_size - 1)) / bpb->sector_size;

}

/*
  		Finds first data sector for given cluster number
 */
uint32_t first_sector_of_cluster(FAT32BootBlock* bpb, uint32_t cluster_num)
{
	uint32_t first_data_sector = bpb->reserved_sectors + (bpb->number_of_fats * bpb->bpb_FATz32)+ root_dir_sector_count(bpb);
	if (first_data_sector * 512 != 100400){
		printf("FIRST DATA SECTOR WARNING: %d\n", first_data_sector);
	}
	return (((cluster_num-2) * (bpb->sectors_per_cluster)) + first_data_sector)*512;
}

/*

	Calculates the offset counted from the beginning of the file to the 
	fat table entry given cluster number. In case of ls, we give it a current_cluster
	and it returns the address of the entry which tells us where directory continues. 

*/
uint32_t cluster_to_byte_address(FAT32BootBlock* bpb, uint32_t cluster_num)
{
	uint32_t this_offset = cluster_num*4;
	uint32_t this_sector_number = bpb->reserved_sectors + (this_offset/bpb->sector_size);
	uint32_t this_ent_offset = this_offset % bpb->sector_size;

	return this_sector_number * bpb->sector_size + this_ent_offset;
}

/*
	Given offset from the beginning of the file, reads in fat entry. 
*/
uint32_t look_up_fat(FAT32BootBlock* bpb, char* fat_image, uint32_t offset)
{
	FILE *ptr_img;
	uint32_t fat_entry;
	ptr_img = fopen(fat_image, "r");
	fseek(ptr_img, offset, SEEK_SET);
	fread(&fat_entry, sizeof(fat_entry),1, ptr_img);
	fclose(ptr_img);
	return (fat_entry);
}

/*
	Gets rid of trailing whitespace in filenames and inserts dots 
	before extensions.
*/
void process_filenames(DirectoryEntry* dir_array, int length)
{
	int i = 0;
	for (i=0; i < length; ++i)
	{
		if (dir_array[i].Attr == 0x10){
			const char s[2] = " ";
			char *token;
			token = strtok(dir_array[i].Name, s);
			strcpy(dir_array[i].Name, token);
		}
		else {
			char *white_space = strstr(dir_array[i].Name," ");
			if ((white_space) && isspace(*white_space) && isalpha(*(white_space+1))){
				*white_space = '.';
			}		uint16_t FstClusLO;     /* byte 26-27;  Low word of this entry's first cluster number.  */

			white_space = strstr(dir_array[i].Name," ");
			if ((white_space) && isspace(*white_space)){
				*white_space = '\0';
			}
		}

	}
}

/*

	Iterate over a directory and read in its contents to the dir_array[16]
	Calls process_filenames to normalize names (insert dots, get rid of garbage, whitespace etc)
*/
void populate_dir(FAT32BootBlock* bpb , uint32_t DirectoryAddress, char* fat_image, DirectoryEntry* dir_array)
{
	int counter;
	int i = 0;

	FILE *ptr_img;
	ptr_img = fopen(fat_image, "r");
	fseek(ptr_img, DirectoryAddress, SEEK_SET);
	// (sector_size)/sizeof(DirectoryEntry) = 512/32 = 16
	for(counter = 0; counter < 16; counter ++){
		fread(&dir_array[counter], sizeof(DirectoryEntry),1,ptr_img);
		//fseek(ptr_img,1,SEEK_CUR);
	}
	fclose(ptr_img);
	process_filenames(dir_array, 16);
}

/* description: pass in a entry and this properly formats the
 * "firstCluster" from the 2 byte segments in the file structure
 */
uint32_t buildClusterAddress(DirectoryEntry * entry) {
    uint32_t addr = 0x00000000;

    addr |=  entry->FstClusHI << 16;
    addr |=  entry->FstClusLO;
    return addr;
}


/* ************************************************************************************************
 *
 * 			FUNCTIONALITY
 *
 ************************************************************************************************ */

/* ************************************************************************************************
	
	args: 
		FAT32BootBlock bpb is a FAT info struct from alike the one in boot block
		char* fat_image is a path to the filesystem image
		uint32_t* current_cluster is a pointer to a variable created in main, 
				init_env sets it to 2 (root cluster on FAT32)
	returns: void


	Reads info from the boot block and stores it inside the bpb variable,
	of type FAT32BootBlock. Per specification, FAT32BootBlock__attribue((packed)), 
	takes care of endianness of the system. 

	Uses global pathname to get the path to the file, could be passed in as an argument,
	and pathname made local. I use full path.

	fread just reads a block of memory of size struct, beacuse it is the same size 
	and structure as the file image first block in 'falls' perfectly into correct attributes 
	of the struct. 


************************************************************************************************ */
void init_env(FAT32BootBlock * bpb, char* fat_image, uint32_t * current_cluster)
{
	FILE *ptr_img;
	ptr_img = fopen(fat_image, "r");
	printf("FILE PATH: %s\n", fat_image);
	if (!ptr_img)
	{
		printf("Unable to open the file image.");
		return;
	}

	fread(bpb,sizeof(FAT32BootBlock),1,ptr_img);
	fclose(ptr_img);
	*current_cluster = bpb-> bpb_rootcluster;
	CLUSTER_PATH[0] = bpb -> bpb_rootcluster;
	CLUSTER_PATH[1] = 0; // 0 indicates invalid entry, end of the potential iteration
	//strcpy(pwd, "/");
	/*int i;
	for (i = 0; i < 128; ++i)
	{
		PATH[i] = malloc(128 * sizeof(char));
	}*/
	//PATH[1] = NULL; // NULL indicates invalid entry, end of the potential iteration
	PATH_INDEX = 0;

}

/*

	args: FAT32BootBlock * bpb
	returns: void

	Prints the information about the partition required in the assignment specification.

*/
void info(FAT32BootBlock * bpb)
{
	printf("Bytes per sector: %d\n", bpb->sector_size);
	printf("Sectors per cluster: %d\n", bpb->sectors_per_cluster);
	printf("Reserved sectors: %d\n", bpb->reserved_sectors);
	printf("Number of FAT tables: %d\n", bpb->number_of_fats);
	printf("FAT size: %d\n", bpb->bpb_FATz32);
	printf("Root cluster number: %d\n", bpb->bpb_rootcluster);
}


/*

	args: 
		FAT32BootBlock * bpb
		char* fat_image is a path to the filesystem image
		uint32_t current_cluster - cluster representing working directory
		int cd (1 or 0, True or False) 
		int size (1 or 0, True or False) 
		char* dirname - name of the direcory to cd to or NULL if cd is False.

		If cd == 0 and size == 0 then print the contents of the directory. 
		If directory takes more then one cluster, look_up_fat function finds the cluster
		where the directory continues. 
		
		if cd == 1, iterate over the files in the current directory, if match is found 
		change the current directory to the specified directory by returning 
		DirectoryEntry.FstClusLO
		
		if size == 1 and match for filename is found return the size of the file
	
	returns: cluster_number


ls DIRNAMEPrint the contents of DIRNAME including the “.” and “..” directories. For simplicity, 
just print each of the directory entries on separate lines (similar to the way ls -l does in Linux 
shells)Print an error if DIRNAME does not exist or is not a directory.
*/
//void ls(const char * directoryName)

uint32_t ls(FAT32BootBlock* bpb, char* fat_image, uint32_t current_cluster, int cd, int size, char* dirname)
{
	DirectoryEntry dir_array[16];
	DirectoryEntry de;
	DirectoryEntry other;

	// get the start of the actual content of the directory
	uint32_t FirstSectorofCluster = first_sector_of_cluster(bpb, current_cluster);

	FILE *ptr_img;
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.");
		return 0;
	}
	

	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	fread(&de,sizeof(DirectoryEntry),1,ptr_img);
	fclose(ptr_img);

	// 
	populate_dir(bpb, FirstSectorofCluster, fat_image, dir_array);
	uint32_t fat_entry = look_up_fat(bpb, fat_image, cluster_to_byte_address(bpb,current_cluster));


	/* if cd is True and dirname != NULL
		try to match given dirname with fetched directory names
	*/
	if (cd == 1 && dirname){
		int j;
		for (j= 0; j < 16; ++j)
		{
			if (strcmp(dirname, "..") == 0 && strcmp(dir_array[j].Name, "..") == 0 && dir_array[j].Attr == 0x10){
				CLUSTER_PATH[PATH_INDEX] = 0; // dummy element in place of current dir
				PATH_INDEX -= 1; // go up in the path
				return CLUSTER_PATH[PATH_INDEX]; //parent_cluster;
			}
			else if (strcmp(dirname, ".") == 0 && strcmp(dir_array[j].Name, ".") == 0 && dir_array[j].Attr == 0x10){
				return current_cluster;
			}
			else if (strcmp(dir_array[j].Name, dirname) == 0 && dir_array[j].Attr == 0x10){
				PATH_INDEX += 1;
				CLUSTER_PATH[PATH_INDEX] = buildClusterAddress(&dir_array[j]); // dir_array[j].FstClusLO;
				CLUSTER_PATH[PATH_INDEX+1] = 0; // dummy element
				return buildClusterAddress(&dir_array[j]); // dir_array[j].FstClusLO;
			}
		}

	}
	/*  if size is True and dirname != NULL
		try to match given dirname with fetched directory names
		For now "A" is hardcoded, once we handle user inputs we will
		compare to dirname
	*/
	else if(size == 1 && dirname)
	{
		int j;
		for (j= 0; j < 16; ++j)
		{
			if (strcmp(dir_array[j].Name, "A") == 0){
				printf("%s\tsize:%d\n",dir_array[j].Name, dir_array[j].FileSize);
			}

		}
	}
	else{ //cd and size false so we are dealing we normal ls, print the content of directory 
		int j;
		for (j= 0; j < 16; ++j)
		{
			if ((dir_array[j].Attr & 0x0F) <= 0 && strlen(dir_array[j].Name) > 0)
				printf("%s\n",dir_array[j].Name);

			//printf("%s\tsize:%d\n",dir_array[j].Name, dir_array[j].FileSize);

		}
	}


	if (fat_entry == 0x0FFFFFF8 || fat_entry == 0x0FFFFFFF){ 
		printf("END OF THE DIRECTORY. \n");
	}else{ // it is not the end of dir, call ls again with cluster_number returned from fat table
		return ls(bpb, fat_image, fat_entry, 0,0, NULL); 
	}


	return current_cluster;


}

/***********************************************************************/

/* MKDIR RELATED BELOW  */

// /////////////////////////// ////////////////////////////////////////// //

int sectorsInDataRegion(FAT32BootBlock* bs) 
{
	int FATSz;
	int TotSec;
	// if(bs->BPB_FATSz16 != 0)
	// 	FATSz = bs->BPB_FATSz16;
	// else
	//bpb_FATz32
	FATSz = bs-> bpb_FATz32;

	// if(bs->BPB_TotSec16 != 0)
	// 	TotSec = bs->BPB_TotSec16;
	// else
	TotSec = bs->total_sectors_long;
	return TotSec - (bs->reserved_sectors + (bs->number_of_fats * FATSz) + root_dir_sector_count(bs));

}

int countOfClusters(FAT32BootBlock* bs) 
{
	// sectors_per_cluster
	return sectorsInDataRegion(bs) / bs->sectors_per_cluster;
}

uint32_t FAT_findFirstFreeCluster(char* fat_image, FAT32BootBlock* bs) 
{
        FILE *fp = fopen(fat_image, "rb+"); 

        int i = current_cluster;
        int k;
        int fatlookup = 1;

        while(fatlookup != 0x00000000)
        {
                k = 0x4000 + i*sizeof(int);
                fseek(fp, k, SEEK_SET);
                fread(&fatlookup, sizeof(int), 1, fp);
                i++;
        }

        fclose(fp);
        return i;
}
/*
uint32_t FAT_findFirstFreeCluster(char* fat_image, FAT32BootBlock* bs) 
{
    uint32_t i = 0;
    uint32_t totalClusters = (uint32_t) countOfClusters(bs);
    while(i < totalClusters) 
    {
        if ((look_up_fat(bs, fat_image, cluster_to_byte_address(bs, i)) == FAT_FREE_CLUSTER))
            break;
        i++;
    }
    return i; // FAT is FULL
}
*/

int FAT_writeFatEntry(char* fat_image, FAT32BootBlock* bs, uint32_t destinationCluster, uint32_t * newFatVal) 
{
    
    FILE* f = fopen(fat_image, "rb+");

    fseek(f, cluster_to_byte_address(bs, destinationCluster), 0);
    fwrite(newFatVal, 4, 1, f);
    fclose(f);
    printf("FAT_writeEntry: wrote->%d to cluster %d", *newFatVal, destinationCluster);
    return 0;
}

// createEntry(&newDirEntry, dirName, extention, TRUE, beginNewDirClusterChain, 0, FALSE, FALSE);
/* description: takes a directory entry and all the necesary info
	and populates the entry with the info in a correct format for
	insertion into a disk.
*/
int createEntry(DirectoryEntry * entry,
			const char * filename, 
			const char * ext,
			int isDir,
			uint32_t firstCluster,
			uint32_t filesize) 
{
	
    //set the same no matter the entry
    entry->NTRes = 0; 
	entry->CrtTimeTenth = 0;

	entry->CrtTime = 0;
	entry->CrtDate = 0;

	entry->LstAccDate = 0;
	entry->WrtTime = 0;
	entry->WrtDate = 0;

    int x;
    for(x = 0; x < MAX_FILENAME_SIZE; x++) {
        if(x < strlen(filename))
            entry->Name[x] = filename[x];
        else
            entry->Name[x] = ' ';
    }
    if (ext)
    {
    	for(x = 0; x < MAX_EXTENTION_SIZE; x++) {
        if(x < strlen(ext))
            entry->Name[MAX_FILENAME_SIZE + x] = ext[x];
        else
            entry->Name[MAX_FILENAME_SIZE + x] = ' ';
    	}
    }
    

    //decompose address
    entry->FstClusLO = firstCluster;
	entry->FstClusHI = firstCluster >> 16;  
	// entry->FstClusLO = current_cluster/0x100;
	// entry->FstClusHI = current_cluster % 0x100;

    if(isDir == TRUE) {
        entry->FileSize = 0;
        entry->Attr = ATTR_DIRECTORY;
	} else {
        entry->FileSize = filesize;
        entry->Attr = ATTR_ARCHIVE;
	}
    return 0; //stops execution so we don't flow out into empty entry config code below
}



uint32_t getLastClusterInChain(char* fat_image,FAT32BootBlock * bs, uint32_t firstClusterVal) 
{
    int size = 1;
    uint32_t lastCluster = firstClusterVal;
    firstClusterVal = (int) look_up_fat(bs, fat_image, cluster_to_byte_address(bs, firstClusterVal));
    //if cluster is empty return cluster number passed in
    if((((firstClusterVal & MASK_IGNORE_MSB) | FAT_FREE_CLUSTER) == FAT_FREE_CLUSTER) )
        return lastCluster;
    //mask the 1st 4 msb, they are special and don't count    
    while((firstClusterVal = (firstClusterVal & MASK_IGNORE_MSB)) < FAT_EOC) {
        lastCluster = firstClusterVal;
        //printf("%08x, result: %d\n", firstClusterVal, (firstClusterVal < 0x0FFFFFF8));
        firstClusterVal = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, firstClusterVal));
    }
    return lastCluster;
        
}

uint32_t FAT_extendClusterChain(char* fat_image, FAT32BootBlock* bs,  uint32_t clusterChainMember) 
{
    uint32_t firstFreeCluster = FAT_findFirstFreeCluster(fat_image, bs);
	uint32_t lastClusterinChain = getLastClusterInChain(fat_image, bs, clusterChainMember);
    //printf("1stfree: %d ", environment.io_writeCluster);
    FAT_writeFatEntry(fat_image, bs, lastClusterinChain, &firstFreeCluster);
    FAT_writeFatEntry(fat_image,bs, firstFreeCluster, &FAT_EOC);
    return firstFreeCluster;
}

uint32_t getFileSizeInClusters(char* fat_image, FAT32BootBlock* bs, uint32_t firstClusterVal) 
{
    uint32_t size = 1;
    firstClusterVal = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, firstClusterVal));
    //printf("\ngetFileSizeInClusters: %d    ", firstClusterVal);
    while((firstClusterVal = (firstClusterVal & MASK_IGNORE_MSB)) < FAT_EOC) {
       
        size++;
        firstClusterVal = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, firstClusterVal));
        // printf("\ngetFileSizeInClusters: %d    ", firstClusterVal);
    }
    return size;
        
}

// finds the absolute byte address of a cluster 
uint32_t byteOffsetOfCluster(FAT32BootBlock* bs, uint32_t clusterNum) 
{
    //return firstSectorofCluster(bs, clusterNum) * bs->BPB_BytsPerSec; 
    return first_sector_of_cluster(bs, clusterNum) * bs->sector_size;

}

// reads in and returns an arbitrary directory entry
DirectoryEntry * readEntry(char* fat_image, FAT32BootBlock* bs, DirectoryEntry * entry, uint32_t clusterNum, int offset)
{
    offset *= 32;
    printf("HERE DIR_ENTRY READ\n");
    uint32_t dataAddress = first_sector_of_cluster(bs, clusterNum);
    
    FILE* f = fopen(fat_image, "r");
    fseek(f, dataAddress + offset, 0);
	fread(entry, sizeof(DirectoryEntry), 1, f);
    
    fclose(f);
    return entry;
}

// finds the absolute byte address of a directory

uint32_t byteOffsetofDirectoryEntry(FAT32BootBlock* bs, uint32_t clusterNum, int offset) {
    printf("\nbyteOffsetofDirectoryEntry: passed in offset->%d\n", offset);
    offset *= 32;
    printf("\nbyteOffsetofDirectoryEntry: offset*32->%d\n", offset);
    uint32_t dataAddress = first_sector_of_cluster(bs, clusterNum);
    printf("\nbyteOffsetofDirectoryEntry: clusterNum: %d, offset: %d, returning: %d\n", clusterNum, offset, (dataAddress + offset));
    return (dataAddress + offset);
}





/*
	Finds an oopn slot for a new directory entry in a given cluster aka current dir
*/
uint32_t FAT_findNextOpenEntry(char* fat_image, FAT32BootBlock* bs, uint32_t pwdCluster) 
{
    // struct DIR_ENTRY dir;
    // struct FILEDESCRIPTOR fd;
	DirectoryEntry dir;
    FILEDESCRIPTOR fd;

    uint32_t dirSizeInCluster = getFileSizeInClusters(fat_image, bs, pwdCluster);
    //printf("dir Size: %d\n", dirSizeInCluster);
    uint32_t clusterCount;
    char fileName[12];
    uint32_t offset;
    uint32_t increment = 2;
    //each dir is a cluster
    for(clusterCount = 0; clusterCount < dirSizeInCluster; clusterCount++) 
    {
        for(; offset < ENTRIES_PER_SECTOR; offset += increment) 
        {
            
			printf("\nFAT_findNextOpenEntry: offset->%d\n", offset);
            readEntry(fat_image, bs, &dir, pwdCluster, offset);
            //printf("\ncluster num: %d\n", pwdCluster);
            //makeFileDecriptor(&dir, &fd);

            if( dir.Name[0] == 0x00 || dir.Name[0] == 0xE5 /*isEntryEmpty(&fd) == TRUE */) {
                //this should tell me exactly where to write my new entry to
                //printf("cluster #%d, byte offset: %d: ", offset, byteOffsetofDirectoryEntry(bs, pwdCluster, offset));             
                return byteOffsetofDirectoryEntry(bs, pwdCluster, offset);
            }
        }
        //pwdCluster becomes the next cluster in the chain starting at the passed in pwdCluster
       pwdCluster = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, pwdCluster)); 
      
    }
    return -1; //cluster chain is full
}

int writeFileEntry(char* fat_image, FAT32BootBlock* bs, DirectoryEntry * entry, uint32_t destinationCluster, bool isDotEntries) 
{
    int dataAddress;
    int freshCluster;
    FILE* f = fopen(fat_image, "rb+");
    
    if(isDotEntries == FALSE) {
        if((dataAddress = FAT_findNextOpenEntry(fat_image,bs, destinationCluster)) != -1) {//-1 means current cluster is at capacity
            fseek(f, dataAddress, 0);
            fwrite (entry , 1 , sizeof(DirectoryEntry) , f );
        } else {
            freshCluster = FAT_extendClusterChain(fat_image,bs, destinationCluster);
            dataAddress = FAT_findNextOpenEntry(fat_image,bs, freshCluster);
            fseek(f, dataAddress, 0);
            fwrite (entry , 1 , sizeof(DirectoryEntry) , f );
        }
    } else {
        DirectoryEntry dotEntry;
        DirectoryEntry dotDotEntry;

        // ----------- ONLY ROOT FOR NOW ------
        //makeSpecialDirEntries(&dotEntry, &dotDotEntry, destinationCluster, environment.pwd_cluster);
        createEntry(&dotEntry, ".", "", TRUE, destinationCluster, 0);	
		createEntry(&dotDotEntry, "..", "", TRUE, current_cluster, 0);	

        //seek to first spot in new dir cluster chin and write the '.' entry
        dataAddress = byteOffsetofDirectoryEntry(bs, destinationCluster, 0);
        fseek(f, dataAddress, 0);
        fwrite (&dotEntry , 1 , sizeof(DirectoryEntry) , f );
        //seek to second spot in new dir cluster chin and write the '..' entry
        dataAddress = byteOffsetofDirectoryEntry(bs, destinationCluster, 1);
        fseek(f, dataAddress, 0);
        fwrite (&dotDotEntry , 1 , sizeof(DirectoryEntry) , f );
    }
     fclose(f);
     return 0;
}
// /* ---------- */
int mkdir(char* fat_image, FAT32BootBlock* bs, const char * dirName, const char * extention, uint32_t targetDirectoryCluster)
{
    // struct DIR_ENTRY newDirEntry;
    DirectoryEntry newDirEntry;
    //write directory entry to pwd
    printf("\t\t*****************starting MKDIR *******************\n");
    uint32_t beginNewDirClusterChain = FAT_findFirstFreeCluster(fat_image, bs); // free cluster
    printf("\t\t*****************Before FAT_writeFatEntry *******************\n");
    FAT_writeFatEntry(fat_image, bs, beginNewDirClusterChain, &FAT_EOC); //mark that its End of Cluster
    printf("\t\t*****************Before CreateEntry *******************\n");
    createEntry(&newDirEntry, dirName, extention, TRUE, beginNewDirClusterChain, 0);
    printf("\t\t*****************Before Write File Entry *******************\n");

    writeFileEntry(fat_image, bs, &newDirEntry, targetDirectoryCluster, FALSE);
    
    //writing dot entries to newly allocated cluster chain
    writeFileEntry(fat_image, bs, &newDirEntry, beginNewDirClusterChain, TRUE);
   return 0;
}
// ================= KAKAREKO END ==================


int main(int argc,char* argv[])
{
    char fat_image[256];
    //char pwd[256];

 	//int path_index = 0;
    FAT32BootBlock bpb;
    
    if(argc==1)
        printf("\nPlease, provide an image path name.\n");
    	//printf("\nFor more info run the program with -h flag option\n");
    if(argc==2)
    {
        strcpy(fat_image,argv[1]);
    }

    // ///////////////////////////////////////////////////////////////////////
    /* 		initialize the environment, read in the boot block  		*/
    init_env(&bpb, fat_image, &current_cluster);
    // ///////////////////////////////////////////////////////////////////////

    char cmd[MAXCHAR];
	char* command = NULL;
	const char* test_dir = "TEST";
	do {
		fgets(cmd, sizeof(cmd), stdin);
		char * args;
		command = strtok(command, "\n"); // get rid of \n from fgets

		command = strtok(cmd, " ");

		// strtok puts /0 in place of delimiter so we move past it and grab the rest of the command
		// we will process args accordingly depending from the command.
		args = command + strlen(command) +1;
		args = strtok(args, "\n"); // get rid of \n from fgets

		if (strcmp(command, "exit") == 0) {
			printf("EXIT\n");
			/*Part 1: exit*/

		} else if ((strcmp(command, "info\n") == 0)) {
			info(&bpb);
			/*Part 2: info*/

		} else if ((strcmp(command, "ls") == 0)) {

			// first cd to the given dir if such exists
			current_cluster = ls(&bpb, fat_image, current_cluster, 1, 0, args);
			// do ls
			ls(&bpb, fat_image, current_cluster, 0, 0, NULL);

			// cd back if we are not in the root
			if (current_cluster != bpb.bpb_rootcluster && strcmp(args, ".") != 0)
				current_cluster = ls(&bpb, fat_image, current_cluster, 1, 0, "..");


			//printf("DirName: %s\n", cmd);
			/*Part 3: ls DIRNAME*/

		} else if ((strcmp(command, "cd") == 0)) {
			current_cluster = ls(&bpb, fat_image, current_cluster, 1, 0, args);

			/*Part 4: cd DIRNAME*/

		} else if ((strcmp(command, "size") == 0)) {
			printf("SIZE\n");
			/*Part 5: size FILENAME*/

		} else if ((strcmp(command, "creat") == 0)) {
			printf("CREAT\n");
			/*Part 6: creat FILENAME*/

		} else if ((strcmp(command, "mkdir") == 0)) {
			printf("MKDIR\n");
			mkdir(fat_image, &bpb, args, NULL, current_cluster);

			/*Part 7: mkdir DIRNAME*/

		} else if ((strcmp(command, "rm") == 0)) {
			printf("RM\n");
			/*Part 8: rm FILENAME*/

		} else if ((strcmp(command, "rmdir") == 0)) {
			printf("RMDIR\n");
			/*Part 9: rmdir DIRNAME*/

		} else if ((strcmp(command, "open") == 0)) {
			printf("OPEN\n");
			/*Part 10: open FILENAMEMODE*/

		} else if ((strcmp(command, "close") == 0)) {
			printf("CLOSE\n");
			/*Part 11: close FILENAME*/

		} else if ((strcmp(command, "read") == 0)) {
			printf("READ\n");
			/*Part 12: read FILENAMEOFFSETSIZE*/

		} else if ((strcmp(command, "write") == 0)) {
			printf("WRITE\n");
			/*Part 13: write FILENAMEOFFSETSIZESTRING*/
		} else {
			printf("UNKNOWN COMMAND\n");

		}
	}while(strcmp(command, "exit") != 0);


 //    info(&bpb);
 //    ls(&bpb, fat_image, current_cluster, 0, 0, NULL);
 //    char* temp = "RED";
 //    //size
	// printf("SIZE\n");

	// ls(&bpb, fat_image, current_cluster, 0, 1, temp);
 //    // cd
	// printf("current cluster before cd: %d\n", current_cluster);
	// current_cluster = ls(&bpb, fat_image, current_cluster, 1,0, temp);
	// printf("current cluster after cd: %d\n", current_cluster);
	// ls(&bpb, fat_image, current_cluster, 0,0, NULL);
	// printf("current cluster before cd: %d\n", current_cluster);
	// current_cluster = ls(&bpb, fat_image, current_cluster, 1,0, temp);
	// printf("current cluster after cd: %d\n", current_cluster);
	// ls(&bpb, fat_image, current_cluster, 0,0, NULL);

	//ls();
    return 0;
}
