#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

#define MAXCHAR 250
#define CHARCOMAND 50

uint32_t FAT_FREE_CLUSTER = 0x00000000;
uint32_t FAT_EOC = 0x0FFFFFF8;
uint32_t MASK_IGNORE_MSB = 0x0FFFFFFF;

#define TRUE 1
#define FALSE 0
typedef int bool;

const uint8_t ATTR_READ_ONLY = 0x01;
const uint8_t ATTR_HIDDEN = 0x02;
const uint8_t ATTR_SYSTEM = 0x04;
const uint8_t ATTR_VOLUME_ID = 0x08;
const uint8_t ATTR_DIRECTORY = 0x10;
const uint8_t ATTR_ARCHIVE = 0x20;
const uint8_t ATTR_LONG_NAME = 0x0F;

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
//uint32_t FAT_EOC = 0x0FFFFFF8;

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

/* ************************************************************************************************
 *
 * 			HELPERS FUNCTIONS
 *
 ************************************************************************************************ */
uint32_t FAT_getFatEntry(char* fat_image, FAT32BootBlock* bs, uint32_t clusterNum);
uint32_t root_dir_sector_count(FAT32BootBlock* bpb)
{
	return ((bpb->root_dir_entries * 32) + (bpb->sector_size - 1)) / bpb->sector_size;

}
/*
  	FirstDataSector is the Sector where data region starts (should be something
	around 100400 in hex)

 */
uint32_t first_data_sector(FAT32BootBlock* bpb)
{
	return bpb->reserved_sectors + (bpb->number_of_fats * bpb->bpb_FATz32)+ root_dir_sector_count(bpb);
}

/*
  		Finds first data sector for given cluster number
 */
uint32_t first_sector_of_cluster(FAT32BootBlock* bpb, uint32_t cluster_num)
{
	return (((cluster_num-2) * (bpb->sectors_per_cluster)) + first_data_sector(bpb))*512;
}

uint32_t cluster_to_byte_address(FAT32BootBlock* bpb, uint32_t cluster_num)
{
	uint32_t this_offset = cluster_num*4;
	uint32_t this_sector_number = bpb->reserved_sectors + (this_offset/bpb->sector_size);
	uint32_t this_ent_offset = this_offset % bpb->sector_size;

	return this_sector_number * bpb->sector_size + this_ent_offset;
}

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
			}
			white_space = strstr(dir_array[i].Name," ");
			if ((white_space) && isspace(*white_space)){
				*white_space = '\0';
			}
		}

	}
}

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



/* ************************************************************************************************
 *
 * 			FUNCTIONALITY
 *
 ************************************************************************************************ */

/* ************************************************************************************************
	
	Reads into the boot block and stores it inside the bpb global variable,
	of type FAT32BootBlock. Per specification, FAT32BootBlock__attribue((packed)), 
	takes care of endianness of the system. 

	Uses global pathname to get the path to the file, could be passed in as an argument,
	and pathname made local. I use full path.

	fread just reads a block of memory of size struct, beacuse it is the same size 
	and structure as the file image first block in 'falls' perfectly into correct attributes 
	of the struct. 


************************************************************************************************ */
void init_env(FAT32BootBlock * bpb, char* fat_image, uint32_t * current_cluster, char* pwd)
{
	FILE *ptr_img;
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.");
		return;
	}

	fread(bpb,sizeof(FAT32BootBlock),1,ptr_img);
	fclose(ptr_img);
	*current_cluster = bpb-> bpb_rootcluster;
	strcpy(pwd, "/");
}
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
ls DIRNAMEPrint the contents of DIRNAME including the “.” and “..” directories. For simplicity, 
just print each of the directory entries on separate lines (similar to the way ls -l does in Linux 
shells)Print an error if DIRNAME does not exist or is not a directory.
*/
//void ls(const char * directoryName)

uint32_t ls(FAT32BootBlock* bpb, char* fat_image, uint32_t current_cluster, int cd, int size, char* dirname)
{
	DirectoryEntry dir_array[16];
	uint32_t FirstSectorofCluster = first_sector_of_cluster(bpb, current_cluster);
	//uint32_t byte_address = cluster_to_byte_address(bpb, FirstSectorofCluster);
	FILE *ptr_img;
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.");
		return 0;
	}
	DirectoryEntry de;
	DirectoryEntry other;

	//printf("RootDirClusterAddr %d\n",byte_address);	

	//printf("FirstSectorofCluster %d\n",FirstSectorofCluster);
	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	fread(&de,sizeof(DirectoryEntry),1,ptr_img);
	
	fclose(ptr_img);
	populate_dir(bpb, FirstSectorofCluster, fat_image, dir_array);
	uint32_t fat_entry = look_up_fat(bpb, fat_image, cluster_to_byte_address(bpb,current_cluster));


	if (cd == 1 && dirname){
		int j;
		for (j= 0; j < 16; ++j)
		{
			if (strcmp(dir_array[j].Name, "GREEN") == 0 && dir_array[j].Attr == 0x10){
				return dir_array[j].FstClusLO;
			}
			else if (strcmp(dir_array[j].Name, "..") == 0 && dir_array[j].Attr == 0x10){
				return dir_array[j].FstClusLO;
			}
		}

	}
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
	else{
		int j;
		for (j= 0; j < 16; ++j)
		{
			if ((dir_array[j].Attr & 0x0F) <= 0 && strlen(dir_array[j].Name) > 0)
				printf("%s\tsize:%d\n",dir_array[j].Name, dir_array[j].FileSize);

		}
	}


	if (fat_entry == 0x0FFFFFF8 || fat_entry == 0x0FFFFFFF){
		printf("END OF THE DIRECTORY. \n");
	}else{
		return ls(bpb, fat_image, fat_entry, 0,0, NULL);
	}


	return current_cluster;


}
//FirstSectorofCluster = FirstDataSector;

	//FirstSectorofCluster = FirstDataSector;
/*
•Make a directory structure like the 
FAT32DirectoryBlock (remember to reserve some space for the long entry)

•Call an ls function ls(int current_cluster_number {should be the first_cluster_number of a directory})

•Function looks up all directories inside the current directory 
( fseek, and i*FAT32DirectoryStructureCreatedByYou, where ‘i’ is a counter)

•Iterate the above step while the  
i*FAT32DirectoryStructureCreatedByYou < sector_size•When that happens, lookup FAT[current_cluster_number]

•If FAT[current_cluster_number] != 0x0FFFFFF8 or 0x0FFFFFFF or 0x00000000, 
then current_cluster_number = FAT[current_cluster_number]. Do step 3 - 5 by resetting i•Else, break
*/

/*	-------------- mkdir -------------------	*/
// int rootDirSectorCount(struct BS_BPB * bs) {
// 	return (bs->BPB_RootEntCnt * 32) + (bs->BPB_BytsPerSec - 1) / bs->BPB_BytsPerSec ;
	
// }
// uint32_t root_dir_sector_count(FAT32BootBlock* bpb) == rootDirSectorCount(struct BS_BPB * bs)
// FAT32BootBlock* bpb
uint32_t FAT_getFatEntry(char* fat_image, FAT32BootBlock* bs, uint32_t clusterNum);
int sectorsInDataRegion(FAT32BootBlock* bs);
int countOfClusters(FAT32BootBlock* bs);
uint32_t getFatAddressByCluster(FAT32BootBlock* bs, uint32_t clusterNum);
int FAT_findFirstFreeCluster(char* fat_image, FAT32BootBlock* bs);
int FAT_writeFatEntry(char* fat_image, FAT32BootBlock* bs, uint32_t destinationCluster, uint32_t * newFatVal);
int FAT_allocateClusterChain(char* fat_image, FAT32BootBlock* bs,  uint32_t clusterNum);
int deconstructClusterAddress(DirectoryEntry * entry, uint32_t cluster);
int createEntry(DirectoryEntry * entry,
			const char * filename, 
			const char * ext,
			int isDir,
			uint32_t firstCluster,
			uint32_t filesize,
            bool emptyAfterThis,
            bool emptyDirectory);
int makeSpecialDirEntries(DirectoryEntry * dot, 
						DirectoryEntry * dotDot,
						uint32_t newlyAllocatedCluster,
						uint32_t pwdCluster );
uint32_t getLastClusterInChain(char* fat_image,FAT32BootBlock * bs, uint32_t firstClusterVal);
uint32_t FAT_extendClusterChain(char* fat_image, FAT32BootBlock* bs,  uint32_t clusterChainMember);
//uint32_t FAT_findNextOpenEntry(FAT32BootBlock* bs, uint32_t pwdCluster);
int getFileSizeInClusters(char* fat_image,FAT32BootBlock* bs, uint32_t firstClusterVal);
uint32_t FAT_findNextOpenEntry(char* fat_image,FAT32BootBlock* bs, uint32_t pwdCluster);

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

uint32_t FAT_getFatEntry(char* fat_image, FAT32BootBlock* bs, uint32_t clusterNum) 
{
    
   // printf("sectorAddress %d\n", sectorAddress);
    FILE* f = fopen(fat_image, "r");
    uint8_t aFatEntry[FAT_ENTRY_SIZE];
    uint32_t FATOffset = clusterNum * 4;
    fseek(f, getFatAddressByCluster(bs, clusterNum), 0);
    fread(aFatEntry, 1, FAT_ENTRY_SIZE, f);
    fclose(f);
    uint32_t fatEntry = 0x00000000;
    //printf("buffer: %x\n",  fatEntry);
    int x;
    for(x = 0; x < 4; x++) {
        fatEntry |= aFatEntry[(FATOffset % FAT_ENTRY_SIZE ) + x] << 8 * x;
       //printf("%08x ", fatEntry);
        
    }
        //printf("%08x ", sector[FATOffset % SECTOR_SIZE]);
    //fatEntry &= 0x0FFFFFFF;
    //printf("cluster: %d, next->%08x\n ",clusterNum, fatEntry);
    
    return fatEntry;
}

uint32_t getFatAddressByCluster(FAT32BootBlock* bs, uint32_t clusterNum)
{
    uint32_t FATOffset = clusterNum * 4;
    uint32_t ThisFATSecNum = bs->reserved_sectors + (FATOffset / bs->sector_size);
    uint32_t ThisFATEntOffset = FATOffset % bs->sector_size;
    //printf("getFatAddressByCluster: %d", (ThisFATSecNum * bs->BPB_BytsPerSec + ThisFATEntOffset));
    return (ThisFATSecNum * bs->sector_size + ThisFATEntOffset); 
}

int FAT_findFirstFreeCluster(char* fat_image, FAT32BootBlock* bs) 
{
    int i = 0;
    int totalClusters = countOfClusters(bs);
    while(i < totalClusters) 
    {
        if(((FAT_getFatEntry(fat_image, bs, i) & MASK_IGNORE_MSB) | FAT_FREE_CLUSTER) == FAT_FREE_CLUSTER)
            break;
        i++;
    }
    return i;
}


// /* ---------- */
// //uint32_t getFatAddressByCluster(struct BS_BPB * bs, uint32_t clusterNum) 

int FAT_writeFatEntry(char* fat_image, FAT32BootBlock* bs, uint32_t destinationCluster, uint32_t * newFatVal) 
{
    
    FILE* f = fopen(fat_image, "r+");

    fseek(f, getFatAddressByCluster(bs, destinationCluster), 0);
    fwrite(newFatVal, 4, 1, f);
    fclose(f);
    printf("FAT_writeEntry: wrote->%d to cluster %d", *newFatVal, destinationCluster);
    return 0;
}

int FAT_allocateClusterChain(char* fat_image, FAT32BootBlock* bs,  uint32_t clusterNum) 
{
	//environment.io_writeCluster = clusterNum;
	FAT_writeFatEntry(fat_image, bs, clusterNum, &FAT_EOC);
	return 0;
}
// /* ---------- */
// /* description: convert uint32_t to hiCluster and loCluster byte array
//  * and stores it into <entry>
//  */ 
int deconstructClusterAddress(DirectoryEntry * entry, uint32_t cluster) 
{
    // entry->loCluster[0] = cluster;
    // entry->loCluster[1] = cluster >> 8;
    // entry->hiCluster[0] = cluster >> 16;
    // entry->hiCluster[1] = cluster >> 24;


	entry->FstClusHI = cluster;     
	entry->FstClusLO = cluster;
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
			uint32_t filesize,
            bool emptyAfterThis,
            bool emptyDirectory) 
{
	/*
	typedef struct
{
		uint8_t Name[11];        byes 0-10;  short name 
		uint8_t Attr;            byte 11;  Set to one of the File Attributes defined in FileSystem.h file 
		uint8_t NTRes;           byte 12; Set value to 0 when a file is created and never modify or look at it after that. 
		uint8_t CrtTimeTenth;    byte 13; 0 - 199, timestamp at file creating time; count of tenths of a sec. 
		uint16_t CrtTime;        byte 14-15;  Time file was created 
		uint16_t CrtDate;        byte 16-17; Date file was created 
		uint16_t LstAccDate;     byte 18-19; Last Access Date. Date of last read or write. 
		uint16_t FstClusHI;      byte 20-21; High word of 
		uint16_t WrtTime;        byte 22-23; Time of last write. File creation is considered a write 
		uint16_t WrtDate;        byte 24-25; Date of last write. Creation is considered a write.  
		uint16_t FstClusLO;      byte 26-27;  Low word of this entry's first cluster number.  
		uint32_t FileSize;       byte 28 - 31; 32bit DWORD holding this file's size in bytes 
}__attribute((packed)) DirectoryEntry;
	*/
    //set the same no matter the entry
    entry->NTRes = 0; 
	entry->CrtTimeTenth = 0;

	entry->CrtTime = 0;
	entry->CrtDate = 0;

	entry->LstAccDate = 0;
	entry->WrtTime = 0;
	entry->WrtDate = 0;

    if(emptyAfterThis == FALSE && emptyDirectory == FALSE) 
    { //if both are false
        int x;
        for(x = 0; x < MAX_FILENAME_SIZE; x++) {
            if(x < strlen(filename))
                entry->Name[x] = filename[x];
            else
                entry->Name[x] = ' ';
        }

        for(x = 0; x < MAX_EXTENTION_SIZE; x++) {
            if(x < strlen(ext))
                entry->Name[MAX_FILENAME_SIZE + x] = ext[x];
            else
                entry->Name[MAX_FILENAME_SIZE + x] = ' ';
        }
        
        deconstructClusterAddress(entry, firstCluster);

        if(isDir == TRUE) {
            entry->FileSize = 0;
            entry->Attr = ATTR_DIRECTORY;
		} else {
            entry->FileSize = filesize;
            entry->Attr = ATTR_ARCHIVE;
		}
        return 0; //stops execution so we don't flow out into empty entry config code below
        
    } else if(emptyAfterThis == TRUE) { //if this isn't true, then the other must be
        entry->Name[0] = 0xE5;
        entry->Attr = 0x00;
    }else {                             //hence no condition check here
        entry->Name[0] = 0x00;
        entry->Attr = 0x00;
    }
    
    //if i made it here we're creating an empty entry and both conditions
    //require this setup
    int x;
    for(x = 1; x < 11; x++) 
        entry->Name[x] = 0x00;
    
	// entry->loCluster[0] = 0x00;
 //    entry->loCluster[1] = 0x00;
 //    entry->hiCluster[0] = 0x00;
 //    entry->hiCluster[1] = 0x00;
	// entry->attributes = 0x00;
	// entry->fileSize = 0;

	entry->FstClusHI = 0x00; 
	entry->FstClusLO = 0x00; 
	entry->Attr = 0x00; 
	entry->FileSize = 0;
    return 0;
}
// /* ---------- */
// //writeFileEntry
int makeSpecialDirEntries(DirectoryEntry * dot, 
						DirectoryEntry * dotDot,
						uint32_t newlyAllocatedCluster,
						uint32_t pwdCluster ) 
{
	createEntry(dot, ".", "", TRUE, newlyAllocatedCluster, 0, FALSE, FALSE);	
	createEntry(dotDot, "..", "", TRUE, pwdCluster, 0, FALSE, FALSE);	
	return 0;
}


uint32_t getLastClusterInChain(char* fat_image,FAT32BootBlock * bs, uint32_t firstClusterVal) 
{
    int size = 1;
    uint32_t lastCluster = firstClusterVal;
    firstClusterVal = (int) FAT_getFatEntry(fat_image, bs, firstClusterVal);
    //if cluster is empty return cluster number passed in
    if((((firstClusterVal & MASK_IGNORE_MSB) | FAT_FREE_CLUSTER) == FAT_FREE_CLUSTER) )
        return lastCluster;
    //mask the 1st 4 msb, they are special and don't count    
    while((firstClusterVal = (firstClusterVal & MASK_IGNORE_MSB)) < FAT_EOC) {
        lastCluster = firstClusterVal;
        //printf("%08x, result: %d\n", firstClusterVal, (firstClusterVal < 0x0FFFFFF8));
        firstClusterVal = FAT_getFatEntry(fat_image, bs, firstClusterVal);
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

int getFileSizeInClusters(char* fat_image, FAT32BootBlock* bs, uint32_t firstClusterVal) 
{
    int size = 1;
    firstClusterVal = (int) FAT_getFatEntry(fat_image ,bs, firstClusterVal);
    //printf("\ngetFileSizeInClusters: %d    ", firstClusterVal);
    while((firstClusterVal = (firstClusterVal & MASK_IGNORE_MSB)) < FAT_EOC) {
       
        size++;
        firstClusterVal = FAT_getFatEntry(fat_image, bs, firstClusterVal);
        // printf("\ngetFileSizeInClusters: %d    ", firstClusterVal);
    }
    return size;
        
}

uint32_t byteOffsetOfCluster(FAT32BootBlock* bs, uint32_t clusterNum) 
{
    //return firstSectorofCluster(bs, clusterNum) * bs->BPB_BytsPerSec; 
    return first_sector_of_cluster(bs, clusterNum) * bs->sector_size;

}

DirectoryEntry * readEntry(char* fat_image, FAT32BootBlock* bs, DirectoryEntry * entry, uint32_t clusterNum, int offset)
{
    offset *= 32;
    uint32_t dataAddress = byteOffsetOfCluster(bs, clusterNum);
    
    FILE* f = fopen(fat_image, "r");
    fseek(f, dataAddress + offset, 0);
	fread(entry, sizeof(DirectoryEntry), 1, f);
    
    fclose(f);
    return entry;
}

uint32_t byteOffsetofDirectoryEntry(FAT32BootBlock* bs, uint32_t clusterNum, int offset) {
    printf("\nbyteOffsetofDirectoryEntry: passed in offset->%d\n", offset);
    offset *= 32;
    printf("\nbyteOffsetofDirectoryEntry: offset*32->%d\n", offset);
    uint32_t dataAddress = byteOffsetOfCluster(bs, clusterNum);
    printf("\nbyteOffsetofDirectoryEntry: clusterNum: %d, offset: %d, returning: %d\n", clusterNum, offset, (dataAddress + offset));
    return (dataAddress + offset);
}

/* description: pass in a entry and this properly formats the
 * "firstCluster" from the 2 byte segments in the file structure
 */
uint32_t buildClusterAddress(DirectoryEntry * entry) {
    uint32_t firstCluster = 0x00000000;
    //FstClusHI;
	//FstClusLO;
    // firstCluster |=  entry->hiCluster[1] << 24;
    // firstCluster |=  entry->hiCluster[0] << 16;
    // firstCluster |=  entry->loCluster[1] << 8;
    // firstCluster |=  entry->loCluster[0];

    firstCluster |=  entry->FstClusHI << 24;
    firstCluster |=  entry->FstClusHI << 16;
    firstCluster |=  entry->FstClusLO << 8;
    firstCluster |=  entry->FstClusLO;
    return firstCluster;
}

/* descriptioin: takes a FILEDESCRIPTOR and checks if the entry it was
 * created from is empty. Helper function
 */ 
bool isEntryEmpty(FILEDESCRIPTOR * fd) {
    if((fd->filename[0] != 0x00) && (fd->filename[0] != 0xE5) )
        return FALSE;
    else
        return TRUE;
}
/* description: takes a directory entry populates a file descriptor 
 * to be used in the file tables
 * */
FILEDESCRIPTOR * makeFileDecriptor(DirectoryEntry * entry, FILEDESCRIPTOR * fd) 
{
    char newFilename[12];
    bzero(fd->filename, 9);
    bzero(fd->extention, 4);
    memcpy(newFilename, entry->Name, 11);
    newFilename[11] = '\0';
    int x;
    for(x = 0; x < 8; x++) {
        if(newFilename[x] == ' ')
            break;
        fd->filename[x] = newFilename[x];
    }
    fd->filename[++x] = '\0';
    for(x = 8; x < 11; x++) {
        if(newFilename[x] == ' ')
            break;
        fd->extention[x - 8] = newFilename[x];
    }
    fd->extention[++x - 8] = '\0';
    if(strlen(fd->extention) > 0) {
        strcpy(fd->fullFilename, fd->filename);
        strcat(fd->fullFilename, ".");
        strcat(fd->fullFilename, fd->extention);
    } else {
        strcpy(fd->fullFilename, fd->filename);
    }
    fd->firstCluster = buildClusterAddress(entry);
	fd->size = entry->FileSize;
	fd->mode = MODE_UNKNOWN;
    if((entry->Attr & ATTR_DIRECTORY) == ATTR_DIRECTORY)
        fd->dir = TRUE;
    else
        fd->dir = FALSE;
    return fd;
}

uint32_t FAT_findNextOpenEntry(char* fat_image, FAT32BootBlock* bs, uint32_t pwdCluster) 
{
    // struct DIR_ENTRY dir;
    // struct FILEDESCRIPTOR fd;
	DirectoryEntry dir;
    FILEDESCRIPTOR fd;

    int dirSizeInCluster = getFileSizeInClusters(fat_image, bs, pwdCluster);
    //printf("dir Size: %d\n", dirSizeInCluster);
    int clusterCount;
    char fileName[12];
    int offset;
    int increment;
    
    for(clusterCount = 0; clusterCount < dirSizeInCluster; clusterCount++) 
    {
    	// ---- na sztywno mam root
        // if(strcmp(environment.pwd, ROOT) == 0) 
        {
            offset = 1;
            increment = 2;
        } 
        // else 
        // {
        //     offset = 0;
        //     increment = 1;
        // }
		
        for(; offset < ENTRIES_PER_SECTOR; offset += increment) 
        {
            // if(strcmp(environment.pwd, ROOT) != 0 && offset == 2) 
            {
                increment = 2;
                offset -= 1;	
                continue;
            }
			printf("\nFAT_findNextOpenEntry: offset->%d\n", offset);
            readEntry(fat_image, bs, &dir, pwdCluster, offset);
            //printf("\ncluster num: %d\n", pwdCluster);
            makeFileDecriptor(&dir, &fd);

            if( isEntryEmpty(&fd) == TRUE ) {
                //this should tell me exactly where to write my new entry to
                //printf("cluster #%d, byte offset: %d: ", offset, byteOffsetofDirectoryEntry(bs, pwdCluster, offset));             
                return byteOffsetofDirectoryEntry(bs, pwdCluster, offset);
            }
        }
        //pwdCluster becomes the next cluster in the chain starting at the passed in pwdCluster
       pwdCluster = FAT_getFatEntry(fat_image, bs, pwdCluster); 
      
    }
    return -1; //cluster chain is full
}

int writeFileEntry(char* fat_image, FAT32BootBlock* bs, DirectoryEntry * entry, uint32_t destinationCluster, bool isDotEntries) 
{
    int dataAddress;
    int freshCluster;
    FILE* f = fopen(fat_image, "r+");
    
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
        makeSpecialDirEntries(&dotEntry, &dotDotEntry, destinationCluster, 2);

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
    uint32_t beginNewDirClusterChain = FAT_findFirstFreeCluster(fat_image, bs);
    FAT_allocateClusterChain(fat_image, bs, beginNewDirClusterChain);
    createEntry(&newDirEntry, dirName, extention, TRUE, beginNewDirClusterChain, 0, FALSE, FALSE);
    writeFileEntry(fat_image, bs, &newDirEntry, targetDirectoryCluster, FALSE);
    
    //writing dot entries to newly allocated cluster chain
    writeFileEntry(fat_image, bs, &newDirEntry, beginNewDirClusterChain, TRUE);
   
}
/*	-----------------------------------		*/


int main(int argc,char* argv[])
{
    char fat_image[256];
    char pwd[256];
    uint32_t current_cluster;
    FAT32BootBlock bpb;

    if(argc==1)
        printf("\nPlease, provide an image path name.\n");
    	//printf("\nFor more info run the program with -h flag option\n");
    if(argc==2)
    {
        strcpy(fat_image,argv[1]);
    }
    printf("pathname is: %s\n", fat_image);
	init_env(&bpb, fat_image, &current_cluster, pwd);
    info(&bpb);
    ls(&bpb, fat_image, current_cluster, 0, 0, NULL);
    printf("current cluster before cd: %d\n", current_cluster);

    //int mkdir(char* fat_image, FAT32BootBlock* bs, const char * dirName, const char * extention, uint32_t targetDirectoryCluster)
    uint32_t targetDirectoryCluster = 2;
    const char * extention = ".";
    const char * dirName = "HUJ";
	int a = mkdir(fat_image, &bpb,  dirName, extention, targetDirectoryCluster);
	printf("	-----------	\n");
	ls(&bpb, fat_image, current_cluster, 0, 0, NULL);    
    printf("cluster after mkdir(HUJ): %d\n", current_cluster);



    char cmd[MAXCHAR];
    fgets(cmd, sizeof(cmd), stdin); 
    printf("the line: %s\n", cmd);
	//char comand[CHARCOMAND];
	char* comand;
	comand = strtok(cmd, " ");
	printf("the comand: %s\n", comand);
	if(strcmp(comand,"exit")==0)
	{
		printf("EXIT\n");
		/*Part 1: exit*/

	}else if((strcmp(comand,"info")==0))
	{
		printf("INFO\n");
		/*Part 2: info*/

	}else if((strcmp(comand,"ls\n")==0))
	{
		printf("LS\n");
		/*Part 3: ls DIRNAME*/
		
	}else if((strcmp(comand,"cd")==0))
	{
		printf("CD\n");
		/*Part 4: cd DIRNAME*/
		
	}else if((strcmp(comand,"size")==0))
	{
		printf("SIZE\n");
		/*Part 5: size FILENAME*/
		
	}else if((strcmp(comand,"creat")==0))
	{
		printf("CREAT\n");
		/*Part 6: creat FILENAME*/
		
	}else if((strcmp(comand,"mkdir")==0))
	{
		printf("MKDIR\n");
		/*Part 7: mkdir DIRNAME*/
		
	}else if((strcmp(comand,"rm")==0))
	{
		printf("RM\n");
		/*Part 8: rm FILENAME*/
		
	}else if((strcmp(comand,"rmdir")==0))
	{
		printf("RMDIR\n");
		/*Part 9: rmdir DIRNAME*/
		
	}else if((strcmp(comand,"open")==0))
	{
		printf("OPEN\n");
		/*Part 10: open FILENAMEMODE*/
		
	}else if((strcmp(comand,"close")==0))
	{
		printf("CLOSE\n");
		/*Part 11: close FILENAME*/
		
	}else if((strcmp(comand,"read")==0))
	{
		printf("READ\n");
		/*Part 12: read FILENAMEOFFSETSIZE*/
		
	}else if((strcmp(comand,"write")==0))
	{
		printf("WRITE\n");
		/*Part 13: write FILENAMEOFFSETSIZESTRING*/
	}else
	{
		printf("UNKNOWN COMMAND\n");
		
	}


 //    printf("pathname is: %s\n", fat_image);
	// init_env(&bpb, fat_image, &current_cluster, pwd);
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
