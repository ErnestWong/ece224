#include "basic_io.h"
#include "LCD.h"
#include "SD_Card.h"
#include "fat.h"
#include "wm8731.h"
#include "contants.h"
#include <math.h>

volatile int push_button;
volatile BYTE cluster_buffer[512] = { 0 };
int playing = 0;
int playback_state = 0;
int switch_rd;
data_file file_data; //holds the data for the file
int cc[5000];
int ONE_SEC_FREQ = 88200;

#ifdef BUTTON_PIO_BASE
static void button_ISR(void* context, alt_u32 id)
{
	push_button = IORD(BUTTON_PIO_BASE, 3) & 0xf;

	printf("button interrupt triggered: %d\n", push_button);
	switch(push_button)
	{
		//button 0 -- stops playing
		case 1:
		playing = 0;
		break;
		//button 1 -- starts playing, set playing to 1
		case 2:
			if (playing == 0)
      { // disable when playing
				playing = 1;
				read_switch();
        LCD_Display(file_data.Name, playback_state);
			}
		break;
		//button 2 -- show next song (if at the end, loop back)
		case 4:
			if (playing == 0)
      { // disable when playing
				next_song();
				read_switch();
        LCD_Display(file_data.Name, playback_state);
			}
		break;
		//button 3 -- shows previous song (if at the beginning, stay there)
		case 8:
			if (playing == 0)
      { // disable when playing
				prev_song();
				read_switch();
        LCD_Display(file_data.Name, playback_state);
			}
		break;
		// default
		default:
		break;
		// do nothing, no more buttons
	}

	// clear all interrupts by resetting edge capture register
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
}
#endif

//reads switch bits and set playback state accordingly
void read_switch()
{
	//AND switch with 111 and put to switch statement
	switch_rd = IORD(SWITCH_PIO_BASE, 0) & 0x7;
	printf("switch num: %d", switch_rd);
	playback_state = switch_rd;
}

void init()
{
	SD_card_init();
	init_mbr();
	init_bs();
	LCD_Init();
	init_audio_codec();
}

void next_song()
{
	//search for file based on file number. if returns 1 (file not found), reset file number to 0
	if (search_for_filetype("WAV", &file_data, 0, 1) == 1)
  {
		file_number = 0;
		search_for_filetype("WAV", &file_data, 0, 1);
	}
	printf("%s\n", file_data.Name);
	LCD_Display(file_data.Name, playback_state);
}

//decrement file number by 2, if negative then set to 0.
//search for file and show on LCD
void prev_song()
{
	file_number -= 2;
	if (file_number < 0)
		file_number = 0;
	search_for_filetype("WAV", &file_data, 0, 1);
	printf("%s\n", file_data.Name);
	LCD_Display(file_data.Name, playback_state);
}

void play_double()
{
	UINT16 tmpLeft, tmpRight;
	int i;
	int sectorNum = 0;

	while (get_rel_sector(&file_data, cluster_buffer, cc, sectorNum) == 0)
  {
		//write LL and RR in buffer, then skip entire LLRR cycle
		for (i = 0; i < 512; i += 8)
    {
			if (playing == 0)
				return;

			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full
			tmpLeft = (cluster_buffer[i + 1] << 8) | (cluster_buffer[i]); //Package 2 8-bit bytes from the
			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full
			tmpRight = (cluster_buffer[i + 3] << 8) | (cluster_buffer[i + 2]);
			IOWR(AUDIO_0_BASE, 0, tmpLeft); //Write the 16-bit variable tmp to the FIFO where it
			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full
			IOWR(AUDIO_0_BASE, 0, tmpRight);
		}
		sectorNum++;
	}
}

void play_normal()
{
	UINT16 tmp;
	int i;
	int sectorNum = 0;

	printf("normal sped\n");

	while (get_rel_sector(&file_data, cluster_buffer, cc, sectorNum) == 0)
  {
		for (i = 0; i < 512; i += 2)
    {
			if (playing == 0)
				return;

			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full
			tmp = (cluster_buffer[i + 1] << 8) | (cluster_buffer[i]); //Package 2 8-bit bytes from the
			IOWR(AUDIO_0_BASE, 0, tmp); //Write the 16-bit variable tmp to the FIFO where it
			//will be processed by the audio CODEC
		}
		sectorNum++;
	}
}

void play_half()
{
	UINT16 tmpLeft, tmpRight;
	int i;
	int sectorNum = 0;

	while (get_rel_sector(&file_data, cluster_buffer, cc, sectorNum) == 0)
  {
		for (i = 0; i < 512; i += 4)
    {
			if (playing == 0)
				return;

			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full
			tmpLeft = (cluster_buffer[i + 1] << 8) | (cluster_buffer[i]); //Package 2 8-bit bytes from the
			//sector buffer array into the
			//single 16-bit variable tmp
			tmpRight = (cluster_buffer[i + 3] << 8) | (cluster_buffer[i + 2]);
			IOWR(AUDIO_0_BASE, 0, tmpLeft);
			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full
			IOWR(AUDIO_0_BASE, 0, tmpRight);
			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full
			IOWR(AUDIO_0_BASE, 0, tmpLeft);
			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full
			IOWR(AUDIO_0_BASE, 0, tmpRight);
			//will be processed by the audio CODEC
		}
		sectorNum++;
	}
}

void play_reverse()
{
	UINT16 tmpLeft, tmpRight;
	int i;
	int sectorNum = (file_data.FileSize / BPB_BytsPerSec) - 3;

	while (get_rel_sector(&file_data, cluster_buffer, cc, sectorNum) == 0)
  {
		for (i = 512 - 1; i > 0; i -= 2)
    {
			if (playing == 0)
				return;

			while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full

			//tmpRight = (cluster_buffer[i] << 8) | (cluster_buffer[i - 1]); //Package 2 8-bit bytes from the
			//sector buffer array into the
			//single 16-bit variable tmp
			tmpLeft = (cluster_buffer[i] << 8) | (cluster_buffer[i - 1]);
			IOWR(AUDIO_0_BASE, 0, tmpLeft);
			//IOWR(AUDIO_0_BASE, 0, tmpRight);

			//Write the 16-bit variable tmp to the FIFO where it
			//will be processed by the audio CODEC
		}
		sectorNum--;
	}
}

void play_delay()
{
    UINT16 tmpLeft, tmpRight;
    int i;
    UINT16 tmp;
    int sectorNum = 0;
    int queue_size = ONE_SEC_FREQ/2;
    UINT16 queue[queue_size];                //queue of 44100 (44100Hz * 2 / 2)
    int pointer = 0;                  //queue pointer started at 0
    int counter = 0;                  //counter to increment until ONE_SEC_FREQ (1 second)

    while(get_rel_sector(&file_data, cluster_buffer, cc, sectorNum) == 0)
    {
        for(i = 0; i < 512; i += 4)
        {

            if (playing == 0)
              return;

            while (IORD(AUD_FULL_BASE, 0)) {}; //wait until the FIFO is not full

            tmpLeft = ( cluster_buffer[ i + 1 ] << 8 ) | ( cluster_buffer[ i ] );
            tmpRight = ( cluster_buffer[ i + 3 ] << 8 ) | ( cluster_buffer[ i + 2 ] );
            if(counter < ONE_SEC_FREQ)
            {
                //set current queue pointer array element to tmpLeft and set tmpLeft to 0 (silence)
                queue[pointer] = tmpLeft;
                tmpLeft = 0;
            }
            else
            {
                //swap queue[pointer] with current tmpLeft
                tmp = queue[pointer];
                queue[pointer] = tmpLeft;
                tmpLeft = tmp;
            }

            IOWR( AUDIO_0_BASE, 0, tmpLeft );       	//write left and right vals
            while (IORD(AUD_FULL_BASE, 0)) {}; 			  //wait until the FIFO is not full
            IOWR( AUDIO_0_BASE, 0, tmpRight );

            counter +=2 ;                              	 //increment the counter
            pointer = (pointer + 1) % queue_size;        //set the pointer mod queue_size (circular)
        }
        sectorNum++;
    }

    tmpRight = 0;   //set tmpRight to 0

    //at the end, play the remaining stuff in the queue (1 second)

    for(i = 0; i < queue_size; i++)
    {
        while (IORD(AUD_FULL_BASE, 0)) {}; 			//wait until the FIFO is not full
        IOWR( AUDIO_0_BASE, 0, queue[pointer] );    //write current queue pointer element to audio
        while (IORD(AUD_FULL_BASE, 0)) {}; 			//wait until the FIFO is not full
        IOWR( AUDIO_0_BASE, 0, tmpRight );          //set right to 0 (silence)
        pointer = (pointer + 1) % queue_size;       //set the pointer mod queue_size (circular)
    }
}

void play_song()
{
	if (playing == 0) // if not playing
		return;

	// display buffer while waiting for cc
	LCD_File_Buffering(file_data.Name);

	UINT32 cc_length = 1 + ceil(file_data.FileSize / (BPB_SecPerClus * BPB_BytsPerSec));
	build_cluster_chain(cc, cc_length, &file_data);

	LCD_Display(file_data.Name, playback_state);

	switch (playback_state)
  {
	case 0x1: //001
		play_double();
		break;
	case 0x2: //010
		play_half();
		break;
	case 0x3: //011
		play_delay();
		break;
	case 0x4: //100
		play_reverse();
		break;
	default:
		play_normal();
	}
	playing = 0;
}

int main()
{
	printf("main\n");
	init();
	alt_irq_register(BUTTON_PIO_IRQ, (void*) 0, button_ISR);
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
	IOWR(BUTTON_PIO_BASE, 2, 0xf);

	//initialize the board with the first song
	next_song();
	while (1)
  {
		play_song();
	}
	return 0;
}
