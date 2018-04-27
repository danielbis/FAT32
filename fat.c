/*Created by:
Daniel Bis dmb16f@my.fsu.edu
Grzegorz Kakareko gk15@my.fsu.edu 
Mark Thomas mtw14@my.fsu.edu
*/

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

#define ENTRIES_PER_SECTOR 16
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

//Struct for open files
typedef struct
{
    uint32_t file_first_cluster_number;
    uint32_t FileSize;
    short mode;
}openFile;

int countClusters(FAT32BootBlock* bs);
uint32_t FAT_find_free_cluster(char* fat_image, FAT32BootBlock* bs);
int write_to_FAT(char* fat_image, FAT32BootBlock* bs, uint32_t destinationCluster, uint32_t newFatVal);
int createEntry(DirectoryEntry * entry,
			const char * filename, 
			const char * ext,
			int isDir,
			uint32_t firstCluster,
			uint32_t filesize );
uint32_t FAT_extendClusterChain(char* fat_image, FAT32BootBlock* bs,  uint32_t clusterChainMember);
//uint32_t dataSector_NextOpen(FAT32BootBlock* bs, uint32_t pwdCluster);
uint32_t dataSector_NextOpen(char* fat_image,FAT32BootBlock* bs, uint32_t pwdCluster);

uint32_t cluster_number(FAT32BootBlock* bpb, char* fat_image, char * filename, uint32_t pwd_cluster_num);
uint32_t cluster_number_where_is_file(FAT32BootBlock* bpb, char* fat_image, char * filename, uint32_t pwd_cluster_num);


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
	before extentions.
*/
void process_filenames(DirectoryEntry* dir_entry)
{
	
	if (dir_entry->Attr == 0x10){
		const char s[2] = " ";
		char *token;
		token = strtok(dir_entry->Name, s);
		strcpy(dir_entry->Name, token);
	}
	else {
		char *white_space = strstr(dir_entry->Name," ");
		if ((white_space) && isspace(*white_space) && isalpha(*(white_space+1))){
			*white_space = '.';
		}		

		white_space = strstr(dir_entry->Name," ");
		if ((white_space) && isspace(*white_space)){
			*white_space = '\0';
		}
	}

}

/*

	Iterate over a directory and read in its contents to the dir_array[16]
	Calls process_filenames to normalize names (insert dots, get rid of garbage, whitespace etc)
*/
/*void populate_dir(FAT32BootBlock* bpb , uint32_t DirectoryAddress, char* fat_image, DirectoryEntry* dir_array)
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
*/

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
	if (!ptr_img)
	{
		printf("Unable to open the file image.");
		return;
	}

	fread(bpb,sizeof(FAT32BootBlock),1,ptr_img);
	fclose(ptr_img);
	*current_cluster = bpb-> bpb_rootcluster;
	
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

uint32_t ls(FAT32BootBlock* bpb, char* fat_image, uint32_t pwd_cluster, int cd, int size, char* dirname)
{
	DirectoryEntry de;
	// get the start of the actual content of the directory
	uint32_t FirstSectorofCluster = first_sector_of_cluster(bpb, pwd_cluster);
	uint32_t counter;
	
	FILE *ptr_img;
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.");
		return 0;
	}
	

	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	for(counter = 0; counter*sizeof(DirectoryEntry) < bpb->sector_size; counter ++){
		fread(&de, sizeof(DirectoryEntry),1,ptr_img);
		process_filenames(&de);
		/* if cd is True and dirname != NULL
		try to match given dirname with fetched directory names
		*/
		if (cd == 1 && dirname){
			if (strcmp(dirname, "..") == 0 && strcmp(de.Name, "..") == 0 && de.Attr == 0x10){
				fclose(ptr_img);
				if (de.FstClusLO == 0 && de.FstClusHI == 0)
                {
					return 2;
                }
				else
                {
					return buildClusterAddress(&de);
                }
			}
			else if (strcmp(dirname, ".") == 0 && strcmp(de.Name, ".") == 0 && de.Attr == 0x10){
                fclose(ptr_img);
				return pwd_cluster;
			}
			else if (strcmp(de.Name, dirname) == 0 && de.Attr == 0x10){
				fclose(ptr_img);
				return buildClusterAddress(&de); // dir_array[j].FstClusLO;
			}	
		}
		/*  if size is True and dirname != NULL
		try to match given dirname with fetched directory names
		For now "A" is hardcoded, once we handle user inputs we will
		compare to dirname
		*/
		else if(size == 1 && dirname)
		{
			
			if (strcmp(de.Name, dirname) == 0){
				printf("%s\tsize:%d\n",de.Name, de.FileSize);
			}

			
		}
		//cd and size false so we are dealing we normal ls, print the content of directory 
		else{ 
			if ((de.Attr & 0x0F) <= 0 && strlen(de.Name) > 0)
				printf("Name %s\n",de.Name);
		}
	}

	fclose(ptr_img);

	uint32_t fat_entry = look_up_fat(bpb, fat_image, cluster_to_byte_address(bpb,pwd_cluster));

	if (fat_entry == 0x0FFFFFF8 || fat_entry == 0x0FFFFFFF){ 
		return current_cluster;
	}else{ // it is not the end of dir, call ls again with cluster_number returned from fat table
		return ls(bpb, fat_image, fat_entry, cd, size, dirname); 
	}


	return current_cluster;


}

/***********************************************************************/

/* MKDIR RELATED BELOW  */

// /////////////////////////// ////////////////////////////////////////// //


int count_clusters(FAT32BootBlock* bs) 
{
	int FATSz;
	int TotSec;
	int sectors_per_region;
	FATSz = bs-> bpb_FATz32;

	TotSec = bs->total_sectors_long;
	sectors_per_region = TotSec - (bs->reserved_sectors + (bs->number_of_fats * FATSz) + root_dir_sector_count(bs));
	// sectors_per_cluster
	return sectors_per_region / bs->sectors_per_cluster;
}


uint32_t FAT_find_free_cluster(char* fat_image, FAT32BootBlock* bs) 
{
    uint32_t i = 0;
    int found = 0;
    uint32_t totalClusters = (uint32_t) count_clusters(bs);
    while(i < totalClusters) 
    {
        if ((look_up_fat(bs, fat_image, cluster_to_byte_address(bs, i)) == FAT_FREE_CLUSTER)){
            found =1;
            break;
        }
        i++;
    }
    if (found == 1)
    	return i;
    else
    	return 0;  // FAT is FULL
}


int write_to_FAT(char* fat_image, FAT32BootBlock* bs, uint32_t destinationCluster, uint32_t newFatVal) 
{
    
    FILE* f = fopen(fat_image, "rb+");

    fseek(f, cluster_to_byte_address(bs, destinationCluster), 0);
    fwrite(&newFatVal, 4, 1, f);
    fclose(f);
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
	strcpy(entry->Name, filename);
    //check for file extention
    if (ext)
    {
    	strcat(entry->Name, " ");
    	strcat(entry->Name, ext);
    }
    

    //  decompose address
    entry->FstClusLO = firstCluster;
	entry->FstClusHI = firstCluster >> 16;  
	// entry->FstClusLO = current_cluster/0x100;
	// entry->FstClusHI = current_cluster % 0x100;

	//  check if directory and set attributes
    if(isDir == TRUE) {
        entry->FileSize = 0;
        entry->Attr = ATTR_DIRECTORY;
	} else {
        entry->FileSize = filesize;
        entry->Attr = ATTR_ARCHIVE;
	}
    return 0; 
}




uint32_t FAT_extendClusterChain(char* fat_image, FAT32BootBlock* bs,  uint32_t pwd_cluster) 
{
    uint32_t temp_cluster = pwd_cluster;
	uint32_t fat_entry = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, temp_cluster));
	
	while (fat_entry != 0x0FFFFFF8 && fat_entry != 0x0FFFFFFF)
	{
		temp_cluster = fat_entry;
		fat_entry = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, temp_cluster));
	}
	uint32_t firstFreeCluster = FAT_find_free_cluster(fat_image, bs);
	
    write_to_FAT(fat_image,bs, firstFreeCluster, FAT_EOC);
    write_to_FAT(fat_image, bs, temp_cluster, firstFreeCluster);
    return firstFreeCluster;
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
    uint32_t dataAddress = first_sector_of_cluster(bs, clusterNum);
    
    FILE* f = fopen(fat_image, "r");
    fseek(f, dataAddress + offset, 0);
	fread(entry, sizeof(DirectoryEntry), 1, f);
    
    fclose(f);
    return entry;
}

// finds the absolute byte address of a directory

uint32_t byteOffsetofDirectoryEntry(FAT32BootBlock* bs, uint32_t clusterNum, int offset) {
    offset *= 32;
    uint32_t dataAddress = first_sector_of_cluster(bs, clusterNum);
    return (dataAddress + offset);
}





/*
	Finds an oopn slot for a new directory entry in a given cluster aka current dir
*/
uint32_t dataSector_NextOpen(char* fat_image, FAT32BootBlock* bs, uint32_t pwdCluster) 
{
    // struct DIR_ENTRY dir;
    // struct FILEDESCRIPTOR fd;
	DirectoryEntry dir;
    FILEDESCRIPTOR fd;

    //printf("dir Size: %d\n", dirSizeInCluster);
    uint32_t clusterCount;
    char fileName[12];
    uint32_t offset = 0;
    uint32_t increment = 2;
    //each dir is a cluster
    for(clusterCount = 0; clusterCount * sizeof(DirectoryEntry) < bs->sector_size; clusterCount++) 
    {
        for(; offset < ENTRIES_PER_SECTOR; offset += increment) 
        {
            
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
    
    if(isDotEntries == FALSE) 
    {
        if((dataAddress = dataSector_NextOpen(fat_image,bs, destinationCluster)) != -1) {//-1 means current cluster is at capacity
            fseek(f, dataAddress, 0);
            fwrite (entry , 1 , sizeof(DirectoryEntry) , f );
        } else {
            freshCluster = FAT_extendClusterChain(fat_image,bs, destinationCluster);
            dataAddress = dataSector_NextOpen(fat_image,bs, freshCluster);
			//  decompose address
	        fseek(f, dataAddress, 0);
            fwrite (entry , 1 , sizeof(DirectoryEntry) , f );
        }
    } else {
        DirectoryEntry dotEntry;
        DirectoryEntry dotDotEntry;

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
    uint32_t beginNewDirClusterChain = FAT_find_free_cluster(fat_image, bs); // free cluster
    write_to_FAT(fat_image, bs, beginNewDirClusterChain, FAT_EOC); //mark that its End of Cluster

    createEntry(&newDirEntry, dirName, extention, TRUE, beginNewDirClusterChain, 0);

    writeFileEntry(fat_image, bs, &newDirEntry, targetDirectoryCluster, FALSE);
    
    //writing dot entries to newly allocated cluster chain
    writeFileEntry(fat_image, bs, &newDirEntry, beginNewDirClusterChain, TRUE);

   return 0;
}


int create(char* fat_image, FAT32BootBlock* bs, const char* filename, const char* extension, uint32_t targetDirectoryCluster)
{
	
	DirectoryEntry newDirEntry;
	uint32_t beginNewDirClusterChain = FAT_find_free_cluster(fat_image, bs); // free cluster
	write_to_FAT(fat_image, bs, beginNewDirClusterChain, FAT_EOC); //mark that its End of Cluster

    createEntry(&newDirEntry, filename, extension, FALSE, beginNewDirClusterChain, 0);

    writeFileEntry(fat_image, bs, &newDirEntry, targetDirectoryCluster, FALSE);

    return 0;
}

int rm_dir(char* fat_image, FAT32BootBlock* bs, char* dirname)
{

	uint32_t pwd_cluster = ls(bs, fat_image, current_cluster, 1, 0, dirname);
	DirectoryEntry dir_entry;
	DirectoryEntry longCheck;
	// check if dirname found and cd successful 
	if (pwd_cluster != current_cluster)
	{
		FILE *ptr_img;
		ptr_img = fopen(fat_image, "rb+");
		if (!ptr_img)
		{
			printf("Unable to open the file image.");
			return 0;
		}
		uint32_t first_normal_file;
		uint32_t first_long = first_sector_of_cluster(bs, pwd_cluster) + 32; // adding 128 bytes to skip long and short entries for . and ..
		fseek(ptr_img, first_long, SEEK_SET);
		fread(&longCheck,sizeof(DirectoryEntry), 1, ptr_img);
		if (strcmp(longCheck.Name,"..") == 0)
			first_normal_file = first_sector_of_cluster(bs, pwd_cluster) + 64; // adding 128 bytes to skip long and short entries for . and ..
		else
			first_normal_file = first_sector_of_cluster(bs, pwd_cluster) + 128;
		
		fclose(ptr_img);
		uint32_t first_sector_parent = first_sector_of_cluster(bs, current_cluster);
		
		ptr_img = fopen(fat_image, "rb+");
		if (!ptr_img)
		{
			printf("Unable to open the file image.");
			return 0;
		}
		

		fseek(ptr_img, first_normal_file, SEEK_SET);
		fread(&dir_entry, sizeof(DirectoryEntry), 1, ptr_img);

		
		

		if (dir_entry.Name[0] != 0x00 && dir_entry.Name[0] != 0xE5) // not empty dir abort
		{
			printf("Could not delete directory. %s\n", dir_entry.Name);
			fclose(ptr_img);
			return -1;
		}
		else
		{
			// seek back to the beginning of the dir and wipe it clean
			fseek(ptr_img, (first_normal_file - 128), SEEK_SET);
			int i;
			unsigned char zero = 0x00;
			for (i = 0; i < 128; ++i){
				fwrite(&zero, 1, 1, ptr_img);

			}
			write_to_FAT(fat_image, bs, pwd_cluster, 0x00);
			uint32_t counter;
			fseek(ptr_img, first_sector_parent, SEEK_SET);
			for(counter = 0; counter*sizeof(DirectoryEntry) < bs->sector_size; counter ++){
				fread(&dir_entry, sizeof(DirectoryEntry),1,ptr_img);
				process_filenames(&dir_entry);
				if (strcmp(dir_entry.Name, dirname) == 0)
				{
					fseek(ptr_img, -32, SEEK_CUR);
					
					for (i = 0; i < 32; ++i){
						fwrite(&zero, 1, 1, ptr_img);

					}
					fclose(ptr_img);
					return 1; //success
				}

			}
		}

		return -1; // couldnt find the dirname
	
	}	
	return -1; //couldnt find the dirname 
}


int rm(char* fat_image, FAT32BootBlock* bs, char* filename)
{
	int pwd_cluster_num = current_cluster;
	int clust_loc = cluster_number_where_is_file(bs, fat_image, filename,pwd_cluster_num);
	
	if(clust_loc == -1)
	{
		printf("The file %s does not exist\n", filename);
		return -1;
	}
	
	/* 
	Finding the file in the directory
	*/
	DirectoryEntry de;
	DirectoryEntry copy_de;
	unsigned char zero = 0x00;
	// get the start of the actual content of the directory
	uint32_t FirstSectorofCluster = first_sector_of_cluster(bs, clust_loc);
	uint32_t counter;
	FILE *ptr_img;
    
	ptr_img = fopen(fat_image, "rb+");
	if (!ptr_img)
	{
		printf("Unable to open the file image.\n");
		return 0;
	}
	
	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	int nth_dir = 0;
	int i;
	for(counter = 0; counter*sizeof(DirectoryEntry) < bs->sector_size; counter ++)
	{
		fread(&de, sizeof(DirectoryEntry),1,ptr_img);
		process_filenames(&de);
		if (strcmp(de.Name, filename) == 0) // Add flag for files
        {
        	copy_de = de;
        	
			//fseek(ptr_img, -sizeof(DirectoryEntry), SEEK_CUR);
			fseek(ptr_img, -32, SEEK_CUR);
			for (i = 0; i < 32; ++i)
			{
				fwrite(&zero, 1, 1, ptr_img);

			}
        	// Teraz wejebac to directory
        	break;

        }
        nth_dir++;
    }
    /*
	Clean the data, add the clusters where data continue to the stack

    */
    int begin_of_fat = buildClusterAddress(&copy_de);

    FirstSectorofCluster = first_sector_of_cluster(bs, begin_of_fat);
    uint32_t fat_entry = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, begin_of_fat));


    //uint32_t first_sector_parent = first_sector_of_cluster(bs, current_cluster);
    uint32_t cluss_to_be_deleted_ratio = copy_de.FileSize/32;
    while(copy_de.FileSize > 0 )
    {
    	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	    if(cluss_to_be_deleted_ratio>1)
	    {
	    	// delete the whole cluster
	    	for (i = 0; i < copy_de.FileSize; ++i)
			{
				fwrite(&zero, 1, 1, ptr_img);

			}
			copy_de.FileSize = 0;
	    }else
	    {
	    	//delete only small part and done
	    	for (i = 0; i < 32; ++i)
			{
				fwrite(&zero, 1, 1, ptr_img);

			}
			copy_de.FileSize = copy_de.FileSize - 32;
	    }
	    cluss_to_be_deleted_ratio = copy_de.FileSize/32;

	    write_to_FAT(fat_image, bs, FirstSectorofCluster, 0x00);
		if (fat_entry == 0x0FFFFFF8 || fat_entry == 0x0FFFFFFF)
			break; // end of cluster chain	    
	    FirstSectorofCluster = first_sector_of_cluster(bs, begin_of_fat);
	    fat_entry = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, FirstSectorofCluster));

	}

    fclose(ptr_img);

	 return 0;
}


int open_filename(FAT32BootBlock* bpb, char* fat_image, uint32_t current_cluster, char * filename, char * mode_str, openFile * array, int *arrLen)
{
	DirectoryEntry de;
	// get the start of the actual content of the directory
	uint32_t FirstSectorofCluster = first_sector_of_cluster(bpb, current_cluster);
	uint32_t counter;
	FILE *ptr_img;
    int mode;
    if (strcmp(mode_str, "r") == 0)
    {
        mode = MODE_READ;
    }
    else if (strcmp(mode_str, "w") == 0)
    {
        mode = MODE_WRITE;
    }
    else if(strcmp(mode_str, "rw") == 0 || strcmp(mode_str, "wr") == 0)
    {
        mode = MODE_BOTH;
    }
    else
    {
        printf("%s is an invalid mode. Valid modes are \"r\", \"w\", \"rw\" or \"wr\"\n", mode_str);
        return 5;
    }
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.\n");
		return 0;
	}
	

	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	for(counter = 0; counter*sizeof(DirectoryEntry) < bpb->sector_size; counter ++){
		fread(&de, sizeof(DirectoryEntry),1,ptr_img);
		process_filenames(&de);
		if (strcmp(de.Name, filename) == 0)
        {
			if (de.Attr != 0x10)
            {
                //Invalid mode for the file
                if ((mode == MODE_WRITE || mode == MODE_BOTH) && de.Attr == ATTR_READ_ONLY)
                {
                    fclose(ptr_img);
                    printf("Unable to open a READ_ONLY file in %s mode\n", mode_str);
                    return 2;
                }
                
                else
                {
                    uint32_t FirstClusterNum = buildClusterAddress(&de);
                    int i;
                    for(i = 0; i < *arrLen; i++)
                    {
                        //File is already open
                        if (array[i].file_first_cluster_number == FirstClusterNum)
                        {
                            fclose(ptr_img);
                            printf("File with first cluster number: %d is already open\n", FirstClusterNum);
                            return 3;
                        }
                    }
                    
                    openFile newFile;
                    newFile.file_first_cluster_number = FirstClusterNum;
                    newFile.mode = mode;
                    newFile.FileSize = de.FileSize;
                    array[*arrLen] = newFile;
                    *arrLen+=1;
                    fclose(ptr_img);
                    printf("Successful open of file with first cluster number:%d in mode:%s\n", FirstClusterNum, mode_str);
                    /*for (i=0; i < *arrLen; i++)
                        printf("%d\n", array[i].file_first_cluster_number);*/
                    return -1;
                }
            }
            
            //Cant open a directory
            else
            {
                fclose(ptr_img);
                printf("Unable to open a directory\n");
                return 1;
            }
		}
	}
    
    uint32_t fat_entry = look_up_fat(bpb, fat_image, cluster_to_byte_address(bpb,current_cluster));

	if (fat_entry == 0x0FFFFFF8 || fat_entry == 0x0FFFFFFF)
	{ 
		fclose(ptr_img);
        //File not found in current directory
        printf("Unable to find a file named %s in the current directory\n", filename);
        return 4;
	}
    else
	{ // not the end of dir
        fclose(ptr_img);
		return open_filename(bpb, fat_image, fat_entry, filename, mode_str, array, arrLen); 
	}
}



int close_filename(FAT32BootBlock* bpb, char* fat_image, uint32_t current_cluster, char * filename, openFile * array, int *arrLen)
{
    DirectoryEntry de;
	// get the start of the actual content of the directory
	uint32_t FirstSectorofCluster = first_sector_of_cluster(bpb, current_cluster);
	uint32_t counter;
	FILE *ptr_img;
    
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.\n");
		return 0;
	}
	
	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	for(counter = 0; counter*sizeof(DirectoryEntry) < bpb->sector_size; counter ++){
		fread(&de, sizeof(DirectoryEntry),1,ptr_img);
		process_filenames(&de);
		if (strcmp(de.Name, filename) == 0)
        {
            uint32_t FirstClusterNum = buildClusterAddress(&de);
            int i;
            int foundFile = 0;
            for(i = 0; i < *arrLen; i++)
            {
                if(array[i].file_first_cluster_number == FirstClusterNum)
                {
                    foundFile = 1;
                }
                
                if (foundFile == 1 && i != *arrLen-1)
                {
                    array[i] = array[i+1];
                }
            }
            
            if (foundFile)
            {
                fclose(ptr_img);
                *arrLen -= 1;
                printf("Successfully removed file with name:%s and first cluster number: %d\n", filename, FirstClusterNum);
                return -1;
            }
            
            else
            {
                fclose(ptr_img);
                printf("File with name:%s and first cluster number: %d is not open\n", filename, FirstClusterNum);
                return 1;
            }
        }
    }

    uint32_t fat_entry = look_up_fat(bpb, fat_image, cluster_to_byte_address(bpb,current_cluster));

	if (fat_entry == 0x0FFFFFF8 || fat_entry == 0x0FFFFFFF)
	{ 
		fclose(ptr_img);
        printf("Unable to locate file with name: %s in current directory\n", filename);
        return 2;
	}
    else
	{ // not the end of dir
        fclose(ptr_img);
		return close_filename(bpb, fat_image, fat_entry, filename, array, arrLen); 
	}
}
/*
	fat_image is the image path
	bs is the BootInfoSector
	offset is the offset from the start of the file in bytes
	size is the amount of bytes that the function reads in
	
	Read the data from a file in the current working directory with the name FILENAME. Start reading
	from the file at OFFSET bytes and stop after reading SIZE bytes. If the OFFSET+SIZE is larger than
	the size of the file, just read size-OFFSET bytes starting at OFFSET.
	Print an error if FILENAME does not exist, if FILENAME is a directory, if the file is not opened for
	reading, or if OFFSET is larger than the size of the file.

*/
int read_file(char* fat_image, FAT32BootBlock* bs, openFile* open_files, int open_files_count, char* filename, int offset, int size)
{
	/*
		MATCH FOR THE OPEN FILENAME
	*/
	// Check if file in directory
	uint32_t first_cluster = cluster_number(bs, fat_image, filename, current_cluster);
	int i;
	int found = 0;
	for (i= 0; i < open_files_count; ++i)
	{
		if (open_files[i].file_first_cluster_number == first_cluster){
			found = 1;
			break;
		}
	}
	if (found == 0){
		printf("File not found in current directory.\n");
		return -1;
	}

	if (offset > open_files[i].FileSize)
	{
		printf("Offset larger then file size.\n");
		return -1; // error 
	}
	else if (size > open_files[i].FileSize)
	{
		size = open_files[i].FileSize;
	}
	else if (offset + size > open_files[i].FileSize)
	{
		size = open_files[i].FileSize - offset;
	}
	

	uint32_t cluster_number = first_cluster; //buildClusterAddress(target_file);
	uint32_t cluster_offset = offset / bs -> sector_size;

	uint32_t bytes_offset = offset % bs -> sector_size;
	uint32_t cluster_size = bs -> sector_size * bs -> sectors_per_cluster;


	uint32_t clusters_to_read = (uint32_t)(cluster_size / size) + (cluster_size % size);

	uint32_t bytes_to_read = size;

	char* data = malloc(size);

	data[size] = '\0';
	while (cluster_offset != 0)
	{
		cluster_number = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, cluster_number)); 
		cluster_offset -= 1;

		if (cluster_number == 0x0000000 || cluster_number == 0xFFFFFFF8)
		{
			printf("Error, broken cluster chain.\n");
			free(data);
			return -1; // error
		}
	}

	FILE *ptr_img;
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.");
		fclose(ptr_img);
		free(data);
		return -1; // error
	}
	
	
	//char temp[2];
	uint32_t cluster_pos = bytes_offset;
	while (bytes_to_read > 0)
	{
		if (bytes_offset == cluster_size){
			cluster_number = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, cluster_number)); 
			bytes_offset = 0;
			if (cluster_number == 0x0FFFFFF8 || cluster_number == 0x0FFFFFFF)
			{ 
				break;
			}
		}

		fseek(ptr_img, (first_sector_of_cluster(bs, cluster_number) + bytes_offset), SEEK_SET);	
		
			//fread(data, sizeof(char), 1, ptr_img);
			//data[1] = '\0';
		
		if (cluster_size - bytes_offset >= bytes_to_read){
			fread(data, bytes_to_read, 1, ptr_img);
			data[bytes_to_read] = '\0';
			bytes_to_read -= bytes_to_read;
		}
		else{
			fread(data, cluster_size - bytes_offset, 1, ptr_img);
			bytes_to_read -= (cluster_size - bytes_offset);
			data[cluster_size - bytes_offset] = '\0';

		}
		printf("%s", data);
		
		cluster_number = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, cluster_number)); 
		bytes_offset = 0;

	}
	fclose(ptr_img);
	free(data);
	//printf("\n");


	return 0;
}


/* Returns cluster number for the filename in the current cluster*/
uint32_t cluster_number(FAT32BootBlock* bpb, char* fat_image, char * filename,uint32_t pwd_cluster_num)
{// (FAT32BootBlock* bpb, char* fat_image, uint32_t current_cluster, char * filename, openFile * array, int *arrLen)
/*write FILENAME OFFSET SIZE STRING*/
/*
Just do the same as read till you start reading (go to the nth cluster, 
then the OFFSET%sector_size bytes since the start of the nth cluster)

Now, ideally, you should fwrite the STRING into the FILE•Initialize a char array of SIZE bytes 
•Make sure to check that FILENAME is OPEN in WR_ONLY or RD_WR mode
•However, there are a bunch of edge cases that can happen:
*/
	// get the start of the actual content of the directory
	DirectoryEntry de;
	uint32_t FirstSectorofCluster = first_sector_of_cluster(bpb, pwd_cluster_num);
	
	uint32_t counter;
	FILE *ptr_img;
    
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.\n");
		return 0;
	}
	
	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	for(counter = 0; counter*sizeof(DirectoryEntry) < bpb->sector_size; counter ++)
	{
		fread(&de, sizeof(DirectoryEntry),1,ptr_img);
		process_filenames(&de);
		if (strcmp(de.Name, filename) == 0)
        {
        	return buildClusterAddress(&de);

        }
    }
    fclose(ptr_img);

	uint32_t fat_entry = look_up_fat(bpb, fat_image, cluster_to_byte_address(bpb,pwd_cluster_num));

	if (fat_entry == 0x0FFFFFF8 || fat_entry == 0x0FFFFFFF)
	{ 
		return 0;
	}else
	{ // it is not the end of dir, call ls again with cluster_number returned from fat table
		//pwd_cluster_num = fat_entry;
		return cluster_number(bpb, fat_image, filename,fat_entry); 
	}

	return 0;
}

uint32_t cluster_number_where_is_file(FAT32BootBlock* bpb, char* fat_image, char * filename,uint32_t pwd_cluster_num)
{// (FAT32BootBlock* bpb, char* fat_image, uint32_t current_cluster, char * filename, openFile * array, int *arrLen)
/*write FILENAME OFFSET SIZE STRING*/
/*
Just do the same as read till you start reading (go to the nth cluster, 
then the OFFSET%sector_size bytes since the start of the nth cluster)

Now, ideally, you should fwrite the STRING into the FILE•Initialize a char array of SIZE bytes 
•Make sure to check that FILENAME is OPEN in WR_ONLY or RD_WR mode
•However, there are a bunch of edge cases that can happen:
*/
	
	// get the start of the actual content of the directory
	DirectoryEntry de;
	uint32_t FirstSectorofCluster = first_sector_of_cluster(bpb, pwd_cluster_num);

	uint32_t counter;
	FILE *ptr_img;
    
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.\n");
		return 0;
	}
	
	fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
	for(counter = 0; counter*sizeof(DirectoryEntry) < bpb->sector_size; counter ++)
	{
		fread(&de, sizeof(DirectoryEntry),1,ptr_img);
		process_filenames(&de);
		if (strcmp(de.Name, filename) == 0)
        {
        	//return buildClusterAddress(&de);
        	return pwd_cluster_num;

        }
    }
    fclose(ptr_img);

	uint32_t fat_entry = look_up_fat(bpb, fat_image, cluster_to_byte_address(bpb,pwd_cluster_num));

	if (fat_entry == 0x0FFFFFF8 || fat_entry == 0x0FFFFFFF)
	{ 
		printf("END OF THE DIRECTORY. and file not found\n");
		return 0;
	}else
	{ // it is not the end of dir, call ls again with cluster_number returned from fat table
		//pwd_cluster_num = fat_entry;
		return cluster_number_where_is_file(bpb, fat_image, filename,fat_entry); 
	}

	return 0;
}


int write_file(char* fat_image, FAT32BootBlock* bs, openFile* open_files, int open_files_count, char * filename, uint32_t offset, uint32_t size,char * string)
{// (FAT32BootBlock* bpb, char* fat_image, uint32_t current_cluster, char * filename, openFile * array, int *arrLen)
/*write FILENAME OFFSET SIZE STRING*/
/*
Just do the same as read till you start reading (go to the nth cluster, 
then the OFFSET%sector_size bytes since the start of the nth cluster)

Now, ideally, you should fwrite the STRING into the FILE•Initialize a char array of SIZE bytes 
•Make sure to check that FILENAME is OPEN in WR_ONLY or RD_WR mode
•However, there are a bunch of edge cases that can happen:
*/

	// Check if file in directory
	uint32_t first_cluster = cluster_number(bs, fat_image, filename, current_cluster);
	int i;
	int found = 0;
	for (i= 0; i < open_files_count; ++i)
	{
		if (open_files[i].file_first_cluster_number == first_cluster){
			found = 1;
			break;
		}
	}
	if (found == 0){
		printf("File not found in current directory.\n");
		return -1;
	}

	if (offset > open_files[i].FileSize)
	{
		printf("Offset larger then file size.\n");
		return -1; // error 
	}
	FILE *ptr_img;
    
	ptr_img = fopen(fat_image, "rb+");
	if (!ptr_img)
	{
		printf("Unable to open the file image.\n");
		return -1;
	}
	
	if (offset + size > open_files[i].FileSize)
	{
		DirectoryEntry de;

		uint32_t file_entry_cluster = cluster_number_where_is_file(bs, fat_image, filename, current_cluster);
		uint32_t FirstSectorofCluster = first_sector_of_cluster(bs, file_entry_cluster);
		uint32_t counter;
		uint32_t new_size = offset + size;
		fseek(ptr_img, FirstSectorofCluster, SEEK_SET);
		for(counter = 0; counter*sizeof(DirectoryEntry) < bs->sector_size; counter ++)
		{
			fread(&de, sizeof(DirectoryEntry),1,ptr_img);
			process_filenames(&de);
			if (strcmp(de.Name, filename) == 0) // Add flag for files
	        {	        	
				//fseek(ptr_img, -sizeof(DirectoryEntry), SEEK_CUR);
				fseek(ptr_img, -4, SEEK_CUR);
				fwrite(&new_size, 4, 1, ptr_img);
				open_files[i].FileSize = new_size;

	        	break;

	        }
	    }
	}

	char* data = malloc(size);

	if (size > strlen(string))
	{
		memcpy(data, string, strlen(string));
		for (i = strlen(string) + 1; i < size; ++i)
			data[i] = '\0';
	}
	else if (strlen(string) > size)
	{
		memcpy(data, string, size);
	}
	else{
		strcpy(data, string);
	}

	data[size] = '\0';


	uint32_t cluster_number = first_cluster; //buildClusterAddress(target_file);
	uint32_t cluster_offset = offset / bs -> sector_size;

	uint32_t bytes_offset = offset % bs -> sector_size;
	uint32_t cluster_size = bs -> sector_size * bs -> sectors_per_cluster;
	

	uint32_t clusters_to_read = (uint32_t)(cluster_size / size) + (cluster_size % size);

	uint32_t bytes_to_write = size;
	uint32_t file_bytes_to_end = open_files[i].FileSize - offset;

	uint32_t cluster_no_copy = cluster_number;

	while (cluster_offset != 0)
	{
		cluster_number = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, cluster_number)); 
		cluster_offset -= 1;
		if (cluster_number == 0x0000000 || cluster_number == 0xFFFFFFF8)
		{
			printf("Error, broken cluster chain.\n");
			fclose(ptr_img);
			free(data);
			return -1; // error
		}
	}


	
	
	int bytes_written = 0;
	while (bytes_to_write > 0)
	{
		fseek(ptr_img, (first_sector_of_cluster(bs, cluster_number) + bytes_offset), SEEK_SET);	
		
		/*if (bytes_offset == cluster_size)
			break;*/
		//fread(data, sizeof(char), 1, ptr_img);
		//data[1] = '\0';

		if (cluster_size - offset > bytes_to_write){
			fwrite(&data[bytes_written], bytes_to_write, 1, ptr_img);
			bytes_to_write -= bytes_to_write;
			bytes_written += bytes_to_write;
		}
		else{
			fwrite(&data[bytes_written], cluster_size - offset, 1, ptr_img);
			bytes_to_write -= (cluster_size - offset);
			bytes_written += (cluster_size - offset);
		}
		
		// zero-out the offset after the first loop
		bytes_offset = 0;

		cluster_number = look_up_fat(bs, fat_image, cluster_to_byte_address(bs, cluster_number)); 
		if ((cluster_number == 0x0FFFFFF8 || cluster_number == 0x0FFFFFFF) && bytes_to_write > 0){
			cluster_number = FAT_extendClusterChain(fat_image, bs, cluster_no_copy);
		}
		cluster_no_copy = cluster_number;

	}
    fclose(ptr_img);

    free(data);
	return 0;
}

int main(int argc,char* argv[])
{
    char fat_image[256];
    //char pwd[256];
    openFile openFilesArray[2048];
    int arr_length = 0;

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
	do {
		fgets(cmd, sizeof(cmd), stdin);
		char * args;
		command = strtok(command, "\n"); // get rid of \n from fgets

		command = strtok(cmd, " ");

		// strtok puts /0 in place of delimiter so we move past it and grab the rest of the command
		// we will process args accordingly depending from the command.
		args = command + strlen(command) +1;
		args = strtok(args, "\n"); // get rid of \n from fgets
        
        int i;
        for (i = 0; i < strlen(command); i++)
        {
            if(command[i] == '\n')
            {
                command[i] = 0;
            }
        }

		if (strcmp(command, "exit") == 0) {
			/*Part 1: exit*/

		} else if ((strcmp(command, "info") == 0)) {
			info(&bpb);

		} else if ((strcmp(command, "ls") == 0)) {

			current_cluster = ls(&bpb, fat_image, current_cluster, 1, 0, args);
			// do ls
			ls(&bpb, fat_image, current_cluster, 0, 0, NULL);

			// cd back if we are not in the root
			if (current_cluster != bpb.bpb_rootcluster && strcmp(args, ".") != 0)
				current_cluster = ls(&bpb, fat_image, current_cluster, 1, 0, "..");
		
		} else if ((strcmp(command, "cd") == 0)) 
		{
			current_cluster = ls(&bpb, fat_image, current_cluster, 1, 0, args);


		} else if ((strcmp(command, "size") == 0)) {
			current_cluster = ls(&bpb, fat_image, current_cluster, 0, 1, args);
		} else if ((strcmp(command, "create") == 0)) {
			if (strchr(args, '.') != NULL)
			{
				char* filename;
				char* extention;
				filename = strtok(args, ".");
				extention = filename + strlen(filename) +1;
				create(fat_image, &bpb, filename, extention, current_cluster);

			}
			else
				create(fat_image, &bpb, args, NULL, current_cluster);

		} else if ((strcmp(command, "mkdir") == 0)) {
			mkdir(fat_image, &bpb, args, NULL, current_cluster);

			/*Part 7: mkdir DIRNAME*/

		} else if ((strcmp(command, "rm") == 0)) {
			rm(fat_image, &bpb, args);
			/*Part 8: rm FILENAME*/

		} else if ((strcmp(command, "rmdir") == 0)) {
			rm_dir(fat_image, &bpb, args);

		} else if ((strcmp(command, "open") == 0)) {
            char* filename;
			char* mode;
			filename = strtok(args, " ");
			mode = filename + strlen(filename) +1;
            open_filename(&bpb, fat_image, current_cluster, filename, mode, openFilesArray, &arr_length);

		} else if ((strcmp(command, "close") == 0)) {
            char * filename;
            filename = strtok(args, " ");
            close_filename(&bpb, fat_image, current_cluster, filename, openFilesArray, &arr_length);

		} else if ((strcmp(command, "read") == 0)) {
			args = command + strlen(command) +1;
			char * filename = strtok(args, " ");
			char * offset_c = strtok(NULL, " ");
			int offset = atoi(offset_c);
			char * size_c = offset_c + strlen(offset_c) +1;
			int read_size = atoi(size_c); 

			read_file(fat_image, &bpb, openFilesArray, arr_length,filename, offset, read_size);

			/*Part 12: read FILENAMEOFFSETSIZE*/

		} else if ((strcmp(command, "write") == 0)) {
			char * filename = strtok(args, " ");
			char* offset_c = strtok(NULL, " ");
			char* size_c = strtok(NULL, " ");
			char * string = size_c + strlen(size_c) + 1;
			strcat(string, "\n");
			int offset = atoi(offset_c);
			int size = atoi(size_c);
			write_file(fat_image,&bpb,  openFilesArray, arr_length, filename, (uint32_t)offset, (uint32_t)size,string);
			


		} else {
			printf("UNKNOWN COMMAND\n");

		}
	}while(strcmp(command, "exit") != 0);


    return 0;
}
