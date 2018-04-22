#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

#define MAXCHAR 250
#define CHARCOMMAND 50

/*
 * Arrays storing our current position in the file
 * for clusters 0 represents first invalid element
 * for PATH NULL represents first invalid element
 * PATH_INDEX represents the index of last element in both tables
 */
uint32_t CLUSTER_PATH[128];  //stores cluster path for easy .. exec
char* PATH[128];  // stores path as an array of dir names for easy .. exec
unsigned int PATH_INDEX;

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
			}
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
	PATH[0] = malloc(3 * sizeof(char));
	strcpy(PATH[0], "/");
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
		For now "Green" is hardcoded, once we handle user inputs we will
		compare to dirname
	*/
	if (cd == 1 && dirname){
		int j;
		for (j= 0; j < 16; ++j)
		{
			if (strcmp(dirname, "..") == 0 && strcmp(dir_array[j].Name, "..") == 0 && dir_array[j].Attr == 0x10){
				CLUSTER_PATH[PATH_INDEX] = 0; // dummy element in place of current dir
				free(PATH[PATH_INDEX]);
				PATH[PATH_INDEX] = NULL; // dummy element in place of current dir
				PATH_INDEX -= 1; // go up in the path
				return CLUSTER_PATH[PATH_INDEX]; //parent_cluster;
			}
			else if (strcmp(dirname, ".") == 0 && strcmp(dir_array[j].Name, ".") == 0 && dir_array[j].Attr == 0x10){
				return current_cluster;
			}
			else if (strcmp(dir_array[j].Name, dirname) == 0 && dir_array[j].Attr == 0x10){
				PATH_INDEX += 1;
				CLUSTER_PATH[PATH_INDEX] = dir_array[j].FstClusLO;
				printf("%s \n", dir_array[j].Name );
				PATH[PATH_INDEX] = malloc(128 * sizeof(char));
				strcpy(PATH[PATH_INDEX],dir_array[j].Name);
				CLUSTER_PATH[PATH_INDEX+1] = 0; // dummy element
				PATH[PATH_INDEX+1] = NULL; // dummy element
				return dir_array[j].FstClusLO;
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



int main(int argc,char* argv[])
{
    char fat_image[256];
    //char pwd[256];

 	//int path_index = 0;
    uint32_t current_cluster;
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
			current_cluster = ls(&bpb, fat_image, current_cluster, 0, 0, NULL);

			// cd back if we are not in the root
			if (current_cluster != bpb.bpb_rootcluster)
				current_cluster = ls(&bpb, fat_image, current_cluster, 1, 0, "..");
			printf("PATH[PATH_INDEX]: %s\n", PATH[PATH_INDEX]);

			//printf("DirName: %s\n", cmd);
			/*Part 3: ls DIRNAME*/

		} else if ((strcmp(command, "cd") == 0)) {
			current_cluster = ls(&bpb, fat_image, current_cluster, 1, 0, args);
			printf("PATH[PATH_INDEX] cd : %s\n", PATH[PATH_INDEX]);

			/*Part 4: cd DIRNAME*/

		} else if ((strcmp(command, "size") == 0)) {
			printf("SIZE\n");
			/*Part 5: size FILENAME*/

		} else if ((strcmp(command, "creat") == 0)) {
			printf("CREAT\n");
			/*Part 6: creat FILENAME*/

		} else if ((strcmp(command, "mkdir") == 0)) {
			printf("MKDIR\n");
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
