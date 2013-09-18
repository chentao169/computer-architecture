#include<stdio.h>
#include<error.h>
#include<stdlib.h>
#include<string.h>

FILE *pinput = NULL;
FILE *poutput = NULL;
unsigned char data[4] = {0,0,0,0};
unsigned int instr = 0;
unsigned long int locate = 584;
char dataflag = 0;
/*
the construction of integer data in file(littele endian)
bit7						bit0
|---|---|---|---|---|---|---|---|   byte0
bit15						bit8
|---|---|---|---|---|---|---|---|   byte1
bit23						bit16
|---|---|---|---|---|---|---|---|   byte2
bit31						bit24
|---|---|---|---|---|---|---|---|   byte3
*/


void SWfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int base = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;    //20bit-16bit
	short int offset = (int)oper&0xffff;		//15bit-0bit	
	
	fprintf(poutput," %ld SW R%d, %d(R%d) \n", locate,rt,offset,base);
	locate += 4;
}

void LWfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int base = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;    //20bit-16bit
	short int offset = (int)oper&0xffff;		//15bit-0bit	
	
	fprintf(poutput," %ld LW R%d, %d(R%d) \n", locate,rt,offset,base);
	locate += 4;
}

void Jfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int instr_index = oper&0x3ffffff;  //25bit-0bit
	unsigned int target = instr_index<<2;	

	fprintf(poutput," %ld J #%d \n", locate,target);
	locate += 4;
}

void BEQfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	int offset = (int)(short int)oper<<2;	
  	
	fprintf(poutput," %ld BEQ R%d, R%d, #%d \n", locate,rs,rt,offset);
	locate += 4;
}

void BNEfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	int offset = (int)(short int)oper<<2;			 
	
	fprintf(poutput," %ld BNE R%d, R%d, #%d \n", locate,rs,rt,offset);
	locate += 4;
}

void BGEZfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	int offset = (int)(short int)oper<<2;	
	
	fprintf(poutput," %ld BGEZ R%d, #%d \n", locate,rs,offset);
	locate += 4;
}
void BGTZfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  	  //25bit-21bit
	int offset = (int)(short int)oper<<2;	
	
	fprintf(poutput," %ld BGTZ R%d, #%d \n", locate,rs,offset); 
	locate += 4;
}

void BLEZfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	int offset = (int)(short int)oper<<2;	
	
	fprintf(poutput," %ld BLEZ R%d, #%d \n", locate,rs,offset);
	locate += 4;
}

void BLTZfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	int offset = (int)(short int)oper<<2;	
	
	fprintf(poutput," %ld BLTZ R%d, #%d \n", locate,rs,offset);
	locate += 4;
}

void ADDIfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	short int imme = (short int)oper;      //15bit-0bit	

	fprintf(poutput," %ld ADDI R%d, R%d, #%i \n", locate,rt,rs,imme);
	locate += 4;
}

void ADDIUfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	short int imme = (short int)oper;      //15bit-0bit	
	
	fprintf(poutput," %ld ADDIU R%d, R%d, #%d \n", locate,rt,rs,imme);
	locate += 4;
}

void BREAKfunc(unsigned int *instruction)
{	
	fprintf(poutput," %ld BREAK \n",locate);
	locate += 4;
	dataflag = 1;
}

void SLTfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld SLT R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void SLTIfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	short int imme = (short int)oper;      //15bit-0bit	
	
	fprintf(poutput," %ld SLTI R%d, R%d, %d \n", locate,rt,rs,imme);
	locate += 4;
}

void SLTUfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld SLTU R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void SLLfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	unsigned int sa = (oper>>6)&0b11111;	  //10bit-6bit	
	
	fprintf(poutput," %ld SLL R%d, R%d, #%d \n", locate,rd,rt,sa);
	locate += 4;
}

void SRLfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	unsigned int sa = (oper>>6)&0b11111;	  //10bit-6bit		
	
	fprintf(poutput," %ld SRL R%d, R%d, #%d \n", locate,rd,rt,sa);
	locate += 4;
}

void SRAfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	unsigned int sa = (oper>>6)&0b11111;	  //10bit-6bit		
	
	fprintf(poutput," %ld SRA R%d, R%d, #%d \n", locate,rd,rt,sa);
	locate += 4;
}

void SUBfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld SUB R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void SUBUfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld SUBU R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void ADDfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld ADD R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void ADDUfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld ADDU R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void ANDfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld AND R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void ORfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld OR R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void XORfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld XOR R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void NORfunc(unsigned int *instruction)
{
	unsigned int oper = *instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	
	fprintf(poutput," %ld NOR R%d, R%d, R%d \n", locate,rd,rs,rt);
	locate += 4;
}

void NOPfunc(unsigned int *instruction)
{	
	fprintf(poutput," %ld NOP \n", locate);
	locate += 4;
}

int REGfunc(unsigned int *instruction)
{
	unsigned int operation = *instruction;
	unsigned int temp = (operation>>16) & 0b111111;
	
	switch(temp)
	{
		case 0b000000:
			BLTZfunc(instruction);
			break;
		case 0b000001:
			BGEZfunc(instruction);
			break;
		default:
			printf("cannot interpret the instruction!\n");
			return -1;
	}
	
	return 0;
	
}

int SPEfunc(unsigned int *instruction)
{
	unsigned int operation = *instruction;
	unsigned int temp = operation & 0b111111;

	switch(temp)
	{
		case 0b001101:
			BREAKfunc(instruction);
			break;
		case 0b101010:
			SLTfunc(instruction);
			break;
		case 0b101011:
			SLTUfunc(instruction);
			break;
		case 0b000000:
			SLLfunc(instruction);
			break;
	    case 0b000010:
			SRLfunc(instruction);
			break;
		case 0b000011:
			SRAfunc(instruction);
			break;
		case 0b100010:
			SUBfunc(instruction);
			break;
		case 0b100011:
			SUBUfunc(instruction);
			break;
		case 0b100000:
			ADDfunc(instruction);
			break;
		case 0b100001:
			ADDUfunc(instruction);
			break;
		case 0b100100:
			ANDfunc(instruction);
			break;
		case 0b100101:
			ORfunc(instruction);
			break;
		case 0b100110:
			XORfunc(instruction);
			break;
		case 0b100111:
			NORfunc(instruction);
			break;
		default:
			printf("cannot interpret the instruction!\n");
			return -1;
	}

	return 0;
}

int Matchoperator(unsigned int *oper)
{
	unsigned int operator = *oper>>26;
	int i = 0;
	
	for(i=31;i>=26;i--)
		fprintf(poutput,"%d",(*oper>>i)&1);
	fprintf(poutput," ");
	for(i=25;i>=21;i--)
		fprintf(poutput,"%d",(*oper>>i)&1);
	fprintf(poutput," ");
	for(i=20;i>=16;i--)
		fprintf(poutput,"%d",(*oper>>i)&1);
	fprintf(poutput," ");
	for(i=15;i>=11;i--)
		fprintf(poutput,"%d",(*oper>>i)&1);
	fprintf(poutput," ");
	for(i=10;i>=6;i--)
		fprintf(poutput,"%d",(*oper>>i)&1);
	fprintf(poutput," ");
	for(i=5;i>=0;i--)
		fprintf(poutput,"%d",(*oper>>i)&1);
	
	if(*oper == 0)
	{
		NOPfunc(oper);
		return 0;
	}

	switch(operator)
	{
		case 0b101011:
			SWfunc(oper);
			break;
		case 0b100011:
			LWfunc(oper);
			break;
		case 0b000010:
			Jfunc(oper);
			break;
		case 0b000100:
			BEQfunc(oper);
			break;
		case 0b000101:
			BNEfunc(oper);
			break;
		case 0b000111:
			BGTZfunc(oper);
			break;
		case 0b000110:
			BLEZfunc(oper);
			break;
		case 0b001000:
			ADDIfunc(oper);
			break; 
		case 0b001001:
			ADDIUfunc(oper);
			break;
		case 0b001010:
			SLTIfunc(oper);
			break;
		case 0b000001:
			REGfunc(oper);
			break;
		case 0b000000:
			SPEfunc(oper);
			break;
		default:
			printf("cannot interpret the instruction!\n");
			return -1;		
	}

	return 0;
	
}

int Readfile(unsigned char *buf, int num, FILE *file) //read data from file 
{
	char cnt = 0;
	cnt = fread(buf, 1, num, file);

	if(cnt != num )
	{
		if(!feof(pinput))
			perror("error occurs when reading data from file!\n");
		return -1;	
	}	

	instr = ((unsigned int) data[0]<< 8*3)|((unsigned int) data[1] << 8*2)|\
			((unsigned int) data[2]<< 8*1)|((unsigned int) data[3] << 8*0);

	return 0;
}

int InterpretData() //interpret data and execute instruction
{
	int i = 0;
	int rv = 0;

	if(dataflag == 1) //judege whether instrucion BREAK is executed
	{
		for(i=31; i>=0; i--)
			fprintf(poutput,"%d",(instr>>i)&1);
		fprintf(poutput," %ld %d \n",locate,(int)instr);
		locate +=4;
		return 0;
	}

	rv = Matchoperator(&instr);
	return rv;
}

int main(int argc, char* argv[])
{
	int rv = 0;
	if(argc < 3)  //estimate the num of arguments
	{
		perror("there are not enough arguments!\n");
		return -1;
	}
		
	pinput = fopen(argv[1], "rb"); //open binary file
	if (pinput == NULL)
    {
        printf("Could not open %s\n" , argv[1]);
        return -__LINE__;
    }
	
	poutput = fopen(argv[2], "w"); //open output file
	if (poutput == NULL)
    {
        printf("Could not open %s\n" , argv[2]);
        return -__LINE__;
    }
	
	do{
		memset(data,0,4);
		instr = 0;
		rv = Readfile(data, 4, pinput);
		if(!rv)	rv = InterpretData();

	}while(rv==0);
	
	fclose(pinput);
	fclose(poutput);
	return 0;

}
