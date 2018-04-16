#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>


char fat_image[256];

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



// GLOBAL booting sector variable
FAT32BootBlock bpb;

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
void info()
{
	FILE *ptr_img;
	ptr_img = fopen(fat_image, "r");
	if (!ptr_img)
	{
		printf("Unable to open the file image.");
		return;
	}

	fread(&bpb,sizeof(FAT32BootBlock),1,ptr_img);
	fclose(ptr_img);
	
	printf("Bytes per sector: %d\n", bpb.sector_size);
	printf("Sectors per cluster: %d\n", bpb.sectors_per_cluster);
	printf("Reserved sectors: %d\n", bpb.reserved_sectors);
	printf("Number of FAT tables: %d\n", bpb.number_of_fats);
	printf("FAT size: %d\n", bpb.bpb_FATz32);
	printf("Root cluster number: %d\n", bpb.bpb_rootcluster);

}

/*
ls DIRNAMEPrint the contents of DIRNAME including the “.” and “..” directories. For simplicity, 
just print each of the directory entries on separate lines (similar to the way ls -l does in Linux 
shells)Print an error if DIRNAME does not exist or is not a directory.
*/
//void ls(const char * directoryName)
void ls()
{
	//struct DIR_ENTRY dir;
	//int useconds = atoi(argv[1]); 
	//FirstSectorofCluster = ((N - 2)*BPB_SecPerClus) + FirstDataSector;
	//int number_of_fats = atoi(bpb.number_of_fats);
	int number_of_fats = bpb.number_of_fats;
	int FirstDataSector = bpb.reserved_sectors + (number_of_fats*bpb.bpb_FATz32);
	// Let's assume we are in the root N=2
	int N=2;
	//int sectors_per_cluster = atoi(bpb.sectors_per_cluster);
	int sectors_per_cluster = bpb.sectors_per_cluster;
	int FirstSectorofCluster = ((N - 2)*sectors_per_cluster) + FirstDataSector;
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


}

int main(int argc,char* argv[])
{
    int counter;
    
    if(argc==1)
        printf("\nPlease, provide an image path name.\n");
    if(argc==2)
    {
        strcpy(fat_image,argv[1]);
    }

    printf("pathname is: %s\n", fat_image);
    info();
    return 0;
}
