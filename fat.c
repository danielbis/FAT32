#include <stdio.h>
#include <string.h>

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
void info(){
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
