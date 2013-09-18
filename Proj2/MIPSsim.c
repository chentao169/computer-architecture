#include<stdio.h>
#include<error.h>
#include<stdlib.h>
#include<string.h>

typedef unsigned char U8;
typedef unsigned int U32;
typedef signed char S8;
typedef	int S32;

FILE *pinput = NULL;
FILE *poutput = NULL;
U8  data[4] = {0,0,0,0};
U32 instr = 0;
U32 locate = 584;

/*
the construction of integer data in file(littele endian)
bit7						       bit0
|---|---|---|---|---|---|---|---|   byte0
bit15						bit8
|---|---|---|---|---|---|---|---|   byte1
bit23						bit16
|---|---|---|---|---|---|---|---|   byte2
bit31						bit24
|---|---|---|---|---|---|---|---|   byte3
*/

enum State{fetch=0, issue, execute, writeresult, commit, print};

typedef struct InstrQueue_e		//instruction queue
{
	U8   ready;   // 0--ready, otherwise--need operands
	U32  instruction;
	S8   op1;	// if not use it, set it -1;
	S8   op2;	// if not use it, set it -1;
	S32  offset;
	S8   des;	// if not use it, set it -1;
	struct InstrQueue_e *pnext;
	struct InstrQueue_e *pprev;
	U8	 speflag;	//for load-store  1-4 for store, 5-10 for load
	U32  CurrPC;	//for branch
	U32  address; 	//for branch
}IQ_E;

typedef struct InstrQueue
{
	IQ_E queue[10];
	IQ_E *used;
	IQ_E *idle;
}IQ;

typedef struct BrachTarBuff_e		
{
	U32 CurrPC;
	U32 TarPC;
	U8  Predict;
	U8  ID;
	struct BrachTarBuff_e *pnext;
	struct BrachTarBuff_e *pprev;
}BTB_E;

typedef struct BrachTarBuff		// branch target buffer
{
	BTB_E queue[10];
	BTB_E *used;
	BTB_E *idle;
}BTB;

typedef struct ResStat_e
{
	S8	ID;		//cosistent with ROB
	U8  ready;   // 0--ready, otherwise--need operands
	U32  instruction;
	U8  nextstate;	//0--cannot go to next stage, 1--can go to next stage
	enum State stage;
	S8  op1;		// if not use it, set it -1;
	S8  op2;		// if not use it, set it -1;
	S32 op1Value;
	S32 op2Value;
	S32 offset;
	S8  des;	// if not use it, set it -1;
	S32 desValue;
	struct ResStat_e *pnext;
	struct ResStat_e *pprev;
	U32 address;	//for load-store and branch
	U8	speflag;	//for load-store ,1-4 for store, 5-10 for load
	U32 CurrPC;		//for branch
}RS_E;

typedef struct ResStation		//reservation station
{
	RS_E queue[8];
	RS_E *used;
	RS_E *idle;
}RS;

typedef struct ReorderBuf_e
{
	S8  ID;
	U32 instruction;
	U8  nextstate;	//0--cannot go to next stage, 1--can go to next stage
	enum State stage;
	S8  des;		// if not use it, set it -1;
	S32 desValue;
	struct ReorderBuf_e *pnext;
	struct ReorderBuf_e *pprev;
	U32 address;	//for load-store
	U8	speflag;	//for load-store, 1-4 for store, 5-10 for load
	U32 CurrPC;		//for branch
}ROB_E;

typedef struct ReorderBuf		// reorder buffer
{
	ROB_E queue[6];
	ROB_E *used;
	ROB_E *idle;
}ROB;

typedef struct RegFile_e
{
	U8  flag;  // 1--busy , 0--idle
	S32 regValue;
}RF;

IQ 	 IQUnit;
BTB  BTBUnit;
ROB  ROBUnit;
RS 	 RSUnit;
U32  ID=0;
RF	 RFUnit[32];
U32  cycle=1;
U32  start;
U32	 end;
int  dataSeg[36];
ROB_E StoreLoad;
U8   entry=0;
U8   stop=0;  //when instr is break ,stop fetch new instr. 0--continue, 1--stop
U8   final=0; // BREAK instr is issued
U8   over=0;  //the program is over
int  cnt=0;  //count the size of data segment= cnt/4

void FetchFun(S8 op1,S8 op2,S8 des,U8 ready,S32 imme,IQ_E *tempIQ)
{
	tempIQ->op1 = op1;
	tempIQ->op2 = op2;
	tempIQ->des = des;
	tempIQ->ready = ready;
	tempIQ->offset = imme;
}

void SWfunc(unsigned int instruction, enum State flag,IQ_E *iq, RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int base = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;    //20bit-16bit
	short    int offset = (int)oper&0xffff;	 //15bit-0bit	

	if(flag == print)
		fprintf(poutput,"[SW R%d, %d(R%d)] \n", rt,offset,base);
	
	if(flag == fetch)  // fetch stage
	{	
		FetchFun(base, -1, rt, 1, offset, iq);
		iq->speflag = 1;
	}
	
	if(flag == execute)
		res->address = res->offset+res->op1Value;
		
}

void LWfunc(unsigned int instruction, enum State flag,IQ_E *iq, RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int base = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;    //20bit-16bit
	short    int offset = (int)oper&0xffff;	 //15bit-0bit	
	
	if(flag == print)
		fprintf(poutput,"[LW R%d, %d(R%d)] \n", rt,offset,base);
	
	if(flag == fetch)  // fetch stage
	{
		FetchFun(base, -1, rt, 1, offset, iq);
		iq->speflag = 5;
	}
			
	if(flag == execute)
		res->address = res->offset+res->op1Value;
}

void Jfunc(unsigned int instruction, enum State flag,IQ_E *iq, RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int instr_index = oper&0x3ffffff;  //25bit-0bit
	unsigned int target = instr_index<<2;	
	
	if(flag == print)
		fprintf(poutput,"[J #%d] \n", target);

	if(flag == fetch)
	{
		FetchFun(-1,-1,-1,0,0,iq);
		iq->speflag=10;
		iq->CurrPC=locate;
		iq->address= ((locate+4)&0xF0000000)|target;
	}
	
	if(flag == execute)
		res->desValue=1;
	
}

void BEQfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	int offset = (int)(short int)oper<<2;	

	if(flag==print)
		fprintf(poutput,"[BEQ R%d, R%d, #%d] \n",rs,rt,offset);

	if(flag==fetch)
	{
		FetchFun(rs, rt, -1, 2, offset, iq);
		iq->address= (U32)iq->offset+locate+4;
		iq->speflag=10;
		iq->CurrPC=locate;
	}

	if(flag == execute)
	{
		if(res->op1Value == res->op2Value)
			res->desValue=1;
		else
			res->desValue=0;
	}
}

void BNEfunc(unsigned int instruction, enum State flag, IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	int offset = (int)(short int)oper<<2;			 

	if(flag==print)
		fprintf(poutput,"[BNE R%d, R%d, #%d] \n", rs,rt,offset);

	if(flag==fetch)
	{
		FetchFun(rs, rt, -1, 2, offset, iq);
		iq->speflag=10;
		iq->address= (U32)iq->offset+locate+4;
		iq->CurrPC=locate;
	}
	if(flag == execute)
	{
		if(res->op1Value != res->op2Value)
			res->desValue=1;
		else
			res->desValue=0;
	}
	
}

void BGEZfunc(unsigned int instruction, enum State flag,IQ_E *iq, RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	int offset = (int)(short int)oper<<2;	
	
	if(flag==print)
		fprintf(poutput,"[BGEZ R%d, #%d] \n", rs,offset);

	if(flag==fetch)
	{
		FetchFun(rs, -1, -1, 1, offset, iq);
		iq->speflag=10;
		iq->address= (U32)iq->offset+locate+4;
		iq->CurrPC=locate;
	}
	if(flag == execute)
	{
		if(res->op1Value >= 0)
			res->desValue=1;
		else
			res->desValue=0;
	}

}
void BGTZfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  	  //25bit-21bit
	int offset = (int)(short int)oper<<2;	

	if(flag==print)
		fprintf(poutput,"[BGTZ R%d, #%d] \n",rs,offset); 

	if(flag==fetch)
	{
		FetchFun(rs, -1, -1, 1, offset, iq);
		iq->speflag=10;
		iq->address= (U32)iq->offset+locate+4;
		iq->CurrPC=locate;
	}
	if(flag == execute)
	{
		if(res->op1Value > 0)
			res->desValue=1;
		else
			res->desValue=0;
	}

}

void BLEZfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	int offset = (int)(short int)oper<<2;	

	if(flag==print)
		fprintf(poutput,"[BLEZ R%d, #%d] \n",rs,offset);

	if(flag==fetch)
	{
		FetchFun(rs, -1, -1, 1, offset, iq);
		iq->speflag=10;
		iq->address= (U32)iq->offset+locate+4;
		iq->CurrPC=locate;
	}
	if(flag == execute)
	{
		if(res->op1Value <= 0)
			res->desValue=1;
		else
			res->desValue=0;
	}

}

void BLTZfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	int offset = (int)(short int)oper<<2;	

	if(flag==print)
		fprintf(poutput,"[BLTZ R%d, #%d] \n", rs,offset);

	if(flag==fetch)
	{
		FetchFun(rs, -1, -1, 1, offset, iq);
		iq->speflag=10;
		iq->address= (U32)iq->offset+locate+4;
		iq->CurrPC=locate;
	}
	if(flag == execute)
	{
		if(res->op1Value < 0)
			res->desValue=1;
		else
			res->desValue=0;
	}
}

void BREAKfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{	
	if(flag==print)
		fprintf(poutput,"[BREAK] \n");

	if(flag==fetch)
	{
		FetchFun(-1, -1, -1, 0, 0, iq);
		iq->speflag=30;
	}
}

void ADDIfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	short int imme = (short int)oper;      //15bit-0bit	

	if(flag==print)
		fprintf(poutput,"[ADDI R%d, R%d, #%i] \n", rt,rs,imme);

	if(flag==fetch)
	{	
		FetchFun(rs, -1, rt, 1, imme, iq);	
		iq->speflag=0;
	}

	if(flag==execute)
		res->desValue=res->op1Value+res->offset;
	
}

void ADDIUfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	short int imme = (short int)oper;      //15bit-0bit	
	
	if(flag==print)
		fprintf(poutput,"[ADDIU R%d, R%d, #%d] \n",rt,rs,imme);
		
	if(flag==fetch)
		FetchFun(rs, -1, rt, 1, imme, iq);	

	if(flag==execute)
		res->desValue=res->op1Value+res->offset;
	
}

void SLTfunc(unsigned int instruction, enum State flag,IQ_E *iq, RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[SLT R%d, R%d, R%d] \n",rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0, iq);

	if(flag==execute)
	{
		if(res->op1Value < res->op2Value)
			res->desValue=1;
		else res->des=0;	
	}
	
}

void SLTIfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	short int imme = (short int)oper;      //15bit-0bit
	
	if(flag==print)
		fprintf(poutput,"[SLTI R%d, R%d, %d] \n", rt,rs,imme);
	
	if(flag==fetch)
		FetchFun(rs, -1, rt, 1, imme, iq);	

	if(flag==execute)
	{
		if((short int)res->op1Value < (short int)res->offset)
			res->desValue=1;
		else res->des=0;	
	}
}

void SLTUfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[SLTU R%d, R%d, R%d] \n", rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0, iq);

	if(flag==execute)
	{
		if((U32)res->op1Value < (U32)res->op2Value)
			res->desValue = 1;
		else res->desValue=0;
	}

}

void SLLfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	unsigned int imme = (oper>>6)&0b11111;	  //10bit-6bit	

	if(flag==print)
		fprintf(poutput,"[SLL R%d, R%d, #%d] \n", rd,rt,imme);

	if(flag==fetch)
		FetchFun(rt, -1, rd, 1, imme, iq);	

	if(flag==execute)
		res->desValue = res->op1Value <<(U8)res->offset;
	
}

void SRLfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	unsigned int imme = (oper>>6)&0b11111;	  //10bit-6bit		

	if(flag==print)
		fprintf(poutput,"[SRL R%d, R%d, #%d] \n", rd,rt,imme);

	if(flag==fetch)
		FetchFun(rt, -1, rd, 1, imme, iq);	

	if(flag==execute)
		res->desValue = (U32)res->op1Value>>(U8)res->offset;
}

void SRAfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	
	unsigned int imme = (oper>>6)&0b11111;	  //10bit-6bit		

	if(flag==print)
		fprintf(poutput,"[SRA R%d, R%d, #%d] \n",rd,rt,imme);

	if(flag==fetch)
		FetchFun(rt, -1, rd, 1, imme, iq);	

	if(flag==execute)
		res->desValue = res->op1Value>>(U8)res->offset;
}

void SUBfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[SUB R%d, R%d, R%d] \n", rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0 ,iq);

	if(flag==execute)
		res->desValue = res->op1Value - res->op2Value;
	
}

void SUBUfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[SUBU R%d, R%d, R%d] \n",rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0 ,iq);

	if(flag==execute)
		res->desValue = res->op1Value - res->op2Value;
	
}

void ADDfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[ADD R%d, R%d, R%d] \n", rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0 ,iq);
	
	if(flag==execute)
		res->desValue = res->op1Value + res->op2Value;

}

void ADDUfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[ADDU R%d, R%d, R%d] \n",rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0 ,iq);

	if(flag==execute)
		res->desValue = res->op1Value + res->op2Value;
	
}

void ANDfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[AND R%d, R%d, R%d] \n",rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0 ,iq);

	if(flag==execute)
		res->desValue = res->op1Value & res->op2Value;

}

void ORfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[OR R%d, R%d, R%d] \n",rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0 ,iq);

	if(flag==execute)
		res->desValue = res->op1Value | res->op2Value;
	
}

void XORfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[XOR R%d, R%d, R%d] \n", rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0 ,iq);

	if(flag==execute)
		res->desValue = res->op1Value ^ res->op2Value;
}

void NORfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int oper = instruction;
	unsigned int rs = (oper>>21)&0b11111;  //25bit-21bit
	unsigned int rt = (oper>>16)&0b11111;  //20bit-16bit
	unsigned int rd = (oper>>11)&0b11111;  //15bit-11bit	

	if(flag==print)
		fprintf(poutput,"[NOR R%d, R%d, R%d] \n",rd,rs,rt);

	if(flag==fetch)
		FetchFun(rs, rt, rd, 2, 0 ,iq);

	if(flag==execute)
		res->desValue = ~(res->op1Value | res->op2Value);

}

void NOPfunc(unsigned int instruction, enum State flag,IQ_E *iq, RS_E *res,ROB_E *rob)
{	
	if(flag==print)
		fprintf(poutput,"[NOP]\n");
	
	if(flag==fetch)
	{
		FetchFun(-1, -1, -1, 0, 0 ,iq);
		iq->speflag=20;
	}
		
}

int REGfunc(unsigned int instruction, enum State flag,IQ_E *iq, RS_E *res,ROB_E *rob)
{
	unsigned int operation = instruction;
	unsigned int temp = (operation>>16) & 0b111111;
	
	switch(temp)
	{
		case 0b000000:
			BLTZfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000001:
			BGEZfunc(instruction, flag, iq, res, rob);
			break;
		default:
			printf("cannot interpret the instruction!%d line\n",__LINE__);
			return -1;
	}
	
	return 0;
	
}

int SPEfunc(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int operation = instruction;
	unsigned int temp = operation & 0b111111;

	switch(temp)
	{
		case 0b001101:
			BREAKfunc(instruction, flag, iq, res, rob);
			break;
		case 0b101010:
			SLTfunc(instruction, flag, iq, res, rob);
			break;
		case 0b101011:
			SLTUfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000000:
			SLLfunc(instruction, flag, iq, res, rob);
			break;
	    case 0b000010:
			SRLfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000011:
			SRAfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100010:
			SUBfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100011:
			SUBUfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100000:
			ADDfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100001:
			ADDUfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100100:
			ANDfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100101:
			ORfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100110:
			XORfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100111:
			NORfunc(instruction, flag, iq, res, rob);
			break;
		default:
			printf("cannot interpret the instruction!%d line\n",__LINE__);
			return -1;
	}

	return 0;
}

int Matchoperator(unsigned int instruction, enum State flag,IQ_E *iq,RS_E *res,ROB_E *rob)
{
	unsigned int operator = instruction>>26;
	
	if(instruction == 0)
	{
		NOPfunc(instruction, flag, iq, res, rob);
		return 0;
	}

	switch(operator)
	{
		case 0b101011:
			SWfunc(instruction, flag, iq, res, rob);
			break;
		case 0b100011:
			LWfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000010:
			Jfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000100:
			BEQfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000101:
			BNEfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000111:
			BGTZfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000110:
			BLEZfunc(instruction, flag, iq, res, rob);
			break;
		case 0b001000:
			ADDIfunc(instruction, flag, iq, res, rob);
			break; 
		case 0b001001:
			ADDIUfunc(instruction, flag, iq, res, rob);
			break;
		case 0b001010:
			SLTIfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000001:
			REGfunc(instruction, flag, iq, res, rob);
			break;
		case 0b000000:
			SPEfunc(instruction, flag, iq, res, rob);
			break;
		case 0b011100:
			BREAKfunc(instruction, flag, iq, res, rob);
			break;
		default:
			printf("cannot interpret the instruction!%d line\n",__LINE__);
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

int InterpretData(unsigned int instruction, enum State flag, IQ_E *iq,RS_E *res,ROB_E *rob) //interpret data and execute instruction
{
	int rv = 0;

	/*if(dataflag == 1) //if instrucion BREAK is executed
	{
		for(i=31; i>=0; i--)
			fprintf(poutput,"%d",(instr>>i)&1);
		fprintf(poutput," %d %d \n",locate,(int)instr);
		locate +=4;
		return 0;
	}
	*/
	rv = Matchoperator(instruction, flag, iq, res, rob);
	return rv;
}

void InitUnits()
{
	int i=0;

	memset(&IQUnit,0, sizeof(IQUnit));
	memset(&BTBUnit,0, sizeof(BTBUnit));
	memset(&ROBUnit,0, sizeof(ROBUnit));
	memset(&RSUnit,0, sizeof(RSUnit));
	memset(RFUnit,0, sizeof(RFUnit));
	memset(dataSeg,0,sizeof(dataSeg));
	
	//init RS linklist
	RSUnit.idle=&RSUnit.queue[0];
	RSUnit.used=NULL;
	RSUnit.queue[0].pprev=NULL;
	RSUnit.queue[7].pnext=NULL;
	for(i=0;i<7;i++)
	{
		RSUnit.queue[i].pnext=&RSUnit.queue[i+1];
		RSUnit.queue[i+1].pprev=&RSUnit.queue[i];
	}
	//init ROB linklist
	ROBUnit.idle=&ROBUnit.queue[0];
	ROBUnit.used=NULL;
	ROBUnit.queue[0].pprev=NULL;
	ROBUnit.queue[5].pnext=NULL;
	for(i=0;i<5;i++)
	{
		ROBUnit.queue[i].pnext=&ROBUnit.queue[i+1];
		ROBUnit.queue[i+1].pprev=&ROBUnit.queue[i];
	}
	//init BTB linklist
	BTBUnit.idle=&BTBUnit.queue[0];
	BTBUnit.used=NULL;
	BTBUnit.queue[0].pprev=NULL;
	BTBUnit.queue[9].pnext=NULL;
	for(i=0;i<9;i++)
	{
		BTBUnit.queue[i].pnext=&BTBUnit.queue[i+1];
		BTBUnit.queue[i+1].pprev=&BTBUnit.queue[i];
	}
	//init IQ linklist
	IQUnit.idle=&IQUnit.queue[0];
	IQUnit.used=NULL;
	IQUnit.queue[0].pprev=NULL;
	IQUnit.queue[9].pnext=NULL;
	for(i=0;i<9;i++)
	{
		IQUnit.queue[i].pnext=&IQUnit.queue[i+1];
		IQUnit.queue[i+1].pprev=&IQUnit.queue[i];
	}
	
}

void FetchStage()
{
	int   rv =0;
	int   i=0;
	IQ_E  *temp=NULL;
	BTB_E *tempBTB=NULL;
		
	memset(data,0,4);
	instr = 0;

	temp=IQUnit.used;
	while(temp)
	{	
		i=0;
		if(temp->instruction==13)
		{
			i=1;
			stop=1;
			break;
		}
		temp=temp->pnext;
	}
	if(!i)
		stop=0;
	
	
	if(!stop && !final)
	{
		if(IQUnit.idle)
		{
			temp=IQUnit.idle;
			if(IQUnit.idle->pnext)
			{
				IQUnit.idle->pnext->pprev=NULL;
				IQUnit.idle=IQUnit.idle->pnext;			
			}	
			else
				IQUnit.idle=NULL;
			if(IQUnit.used)
			{
				IQUnit.used->pprev=temp;
				temp->pnext=IQUnit.used;
				temp->pprev=NULL;
				IQUnit.used=temp;
			}
			else
			{
				temp->pnext=NULL;
				temp->pprev=NULL;
				IQUnit.used=temp;	
			}
			temp->address=0;
			temp->CurrPC=0;
			temp->des=0;
			temp->instruction=0;
			temp->offset=0;
			temp->op1=0;
			temp->op2=0;
			temp->ready=0;
			temp->speflag=0;
			
			rv = Readfile(data, 4, pinput);
			if(!rv)	
			{
				temp->instruction= instr;
				rv= InterpretData(temp->instruction, fetch, temp,0,0);
				if(rv==-1) printf("something wrong! %d\n",__LINE__);

				tempBTB=BTBUnit.used;
				if(temp->speflag==10) //for branch
				{
					i=0;
					while(tempBTB)
					{
						if(tempBTB->CurrPC==temp->CurrPC)
						{	
							i=1;
							if(tempBTB->Predict==1)  //predict taken
							{
								locate=tempBTB->TarPC;
								fseek(pinput,locate-584L,SEEK_SET);
								break;
							}
							else	//predict not-taken
							{
								locate=locate+4;
							}
						}
						tempBTB=tempBTB->pnext;
					}
					
				
					if(!i)  //add new branch to BTB
					{
						tempBTB=BTBUnit.idle;
						if(tempBTB)		// there is idle BTB entry
						{
							if(BTBUnit.idle->pnext)
							{
								BTBUnit.idle->pnext->pprev=NULL;
								BTBUnit.idle=BTBUnit.idle->pnext;			
							}	
							else
								BTBUnit.idle=NULL;
							if(BTBUnit.used)
							{
								BTBUnit.used->pprev=tempBTB;
								tempBTB->pnext=BTBUnit.used;
								tempBTB->pprev=NULL;
								BTBUnit.used=tempBTB;
							}
							else
							{
								tempBTB->pnext=NULL;
								tempBTB->pprev=NULL;
								BTBUnit.used=tempBTB;	
							}
							entry++;
							tempBTB->ID=entry;
							
						}
						else		// no BTB entry, replace the oldese entry
						{
							tempBTB=BTBUnit.used;
							entry=9;
							tempBTB->ID=entry;
							while(tempBTB->pnext)
							{
								tempBTB=tempBTB->pnext;
								tempBTB->ID=--entry;
							}
							tempBTB->pprev->pnext=NULL;
							tempBTB->pnext=BTBUnit.used;
							tempBTB->pprev=NULL;
							BTBUnit.used->pprev=tempBTB;
							BTBUnit.used=tempBTB;
							
							tempBTB->ID=10;
						}
						
						//update BTB entry
						tempBTB->CurrPC=temp->CurrPC;
						locate=locate+4; 		//treated as non-branch instruction
						tempBTB->Predict=2;     //not set
						tempBTB->TarPC=temp->address;
														
					}
				}
				else	locate = locate+4;	
			}
		}
	}
}

void IssueStage()
{
	int   i=0;
	RS_E  *tempRS=NULL;
	ROB_E *tempROB=NULL;
	IQ_E  *tempIQ=NULL;

	if(IQUnit.used)
	{	
		
		if(RSUnit.idle && ROBUnit.idle && \
			(IQUnit.used->pnext||IQUnit.used->instruction==13))
		{			
			tempIQ=IQUnit.used;
			while(tempIQ->pnext)
				tempIQ=tempIQ->pnext;

			if(tempIQ->instruction==13)
				final=1;
			
			//issue instr to ROB
			tempROB=ROBUnit.idle;
			if(ROBUnit.idle->pnext)
			{
				ROBUnit.idle->pnext->pprev=NULL;
				ROBUnit.idle=ROBUnit.idle->pnext;			
			}	
			else
				ROBUnit.idle=NULL;
			if(ROBUnit.used)
			{
				ROBUnit.used->pprev=tempROB;
				tempROB->pnext=ROBUnit.used;
				tempROB->pprev=NULL;
				ROBUnit.used=tempROB;
			}
			else
			{
				tempROB->pnext=NULL;
				tempROB->pprev=NULL;
				ROBUnit.used=tempROB;	
			}
			tempROB->ID = ID;
			tempROB->instruction=tempIQ->instruction;
			tempROB->nextstate=1;
			tempROB->des=tempIQ->des;
			tempROB->desValue=0;
			tempROB->stage=0;
			tempROB->speflag=tempIQ->speflag;
			if(tempIQ->speflag==10)
			{
				tempROB->CurrPC=tempIQ->CurrPC;
				tempROB->address=tempIQ->address;
			}
			if(tempIQ->speflag>=20)
			{	
				tempROB->stage=writeresult;
				tempROB->nextstate=0;
			}
				
			//issue instr to RS
			if(tempIQ->speflag<20)
			{
				tempRS=RSUnit.idle;
				if(RSUnit.idle->pnext)
				{
					RSUnit.idle->pnext->pprev=NULL;
					RSUnit.idle=RSUnit.idle->pnext;			
				}	
				else
					RSUnit.idle=NULL;
				if(RSUnit.used)
				{
					RSUnit.used->pprev=tempRS;
					tempRS->pnext=RSUnit.used;
					tempRS->pprev=NULL;
					RSUnit.used=tempRS;
				}
				else
				{
					tempRS->pnext=NULL;
					tempRS->pprev=NULL;
					RSUnit.used=tempRS;	
				}
				tempRS->ID = ID;
				tempRS->ready = tempIQ->ready;
				tempRS->instruction= tempIQ->instruction;
				tempRS->nextstate = 1;
				tempRS->stage=0;
				tempRS->op1 = tempIQ->op1;
				tempRS->op1Value=0;
				tempRS->op2 = tempIQ->op2;
				tempRS->op2Value=0;
				tempRS->offset = tempIQ->offset;
				tempRS->des = tempIQ->des;
				tempRS->desValue=0;
				tempRS->speflag=tempIQ->speflag;
				if(tempIQ->speflag==10)
				{
					tempRS->CurrPC=tempIQ->CurrPC;
					tempRS->address=tempIQ->address;
				}
			}
		
			ID++;
			
			// check the operand in ROB
			if(tempIQ->speflag<20)
			{
				tempROB=ROBUnit.used;
				while(tempROB)
				{
					if((tempROB->stage==writeresult) && (tempROB->des>=0))
					{
						if(tempROB->des == tempRS->op1)
						{
							tempRS->op1Value = tempROB->desValue;
							tempRS->ready--;
						}
						else
						{
							if(tempROB->des== tempRS->op2)
							{
								tempRS->op2Value = tempROB->desValue;
								tempRS->ready--;
							}
						}
					}
					tempROB=tempROB->pnext;
				}
			
			
				for(i=0;i<32;i++)  //check the operand in Register file
				{
					if(!RFUnit[i].flag) //0--not busy
					{	
						if(tempRS->op1==i)
						{
							tempRS->op1Value = RFUnit[i].regValue;
							tempRS->ready--;
						}
						else if(tempRS->op2==i)
						{
							tempRS->op2Value = RFUnit[i].regValue;
							tempRS->ready--;
						}
					}
				}

			}

			//release instr in IQ		
			if(tempIQ->pprev)
				tempIQ->pprev->pnext=NULL;
			else
				IQUnit.used=NULL;
			memset(tempIQ,0,sizeof(IQ_E));
			tempIQ->pnext=IQUnit.idle;
			if(IQUnit.idle)
				IQUnit.idle->pprev=tempIQ;
			IQUnit.idle=tempIQ;
			
		}
	}
	else return;
	
}

void ExeStage()
{
	RS_E  *tempRS=RSUnit.used;
	ROB_E *tempROB=NULL;
	
	while(tempRS)
	{
		if(!tempRS->ready && tempRS->stage==issue)
		{
			InterpretData(tempRS->instruction, execute, 0,tempRS,0);
			tempRS->nextstate=1;
			
			tempROB=ROBUnit.used->pnext;	//cosistent with RS
			while(tempROB)
			{
				if(tempROB->ID==tempRS->ID)
				{
					tempROB->nextstate=1;
					tempROB->stage=tempRS->stage;
				}
				tempROB=tempROB->pnext;				
			}
		}
		tempRS=tempRS->pnext;
	}
}

void WriteStage()
{
	int   i=0;
	RS_E  *tempRS1=RSUnit.used;
	RS_E  *tempRS2=NULL;
	ROB_E *tempROB=NULL;

	while(tempRS1)
	{
		if(tempRS1->stage==execute)
		{			
			tempROB=ROBUnit.used;		// consistent with ROB
			while(tempROB)
			{
				if(tempRS1->ID==tempROB->ID && tempRS1->speflag==0)
				{	
					tempROB->desValue=tempRS1->desValue;
					tempROB->stage=tempRS1->stage;
					tempROB->nextstate= 1;
					tempRS1->nextstate= 1;
					break;
				}
				tempROB=tempROB->pnext;
			}

			if(tempRS1->des>=0&&tempRS1->speflag==0)
			{
				tempRS2=RSUnit.used;     //broadcast the ready result to other RS
				while(tempRS2)
				{
					if(tempRS2->stage==issue \
					||(tempRS2->stage==fetch && tempRS2->nextstate==1))
					{
						if(tempRS2->op1==tempRS1->des)
						{
							tempRS2->op1Value=tempRS1->desValue;
							tempRS2->ready--;						
						}
						if(tempRS2->op2==tempRS1->des)
						{
							tempRS2->op2Value=tempRS1->desValue;
							tempRS2->ready--;
						}
					}
					tempRS2=tempRS2->pnext;
				}
			}

			//write back load address and detect memory dependence 
			if(tempRS1->speflag==5)	
			{
				tempROB=ROBUnit.used;
				while(tempROB)  // write address to ROB
				{
					if(tempROB->ID==tempRS1->ID)
					{
						tempROB->address=tempRS1->address;
						break;
					}
					tempROB=tempROB->pnext;
				}
				
				i=0;
				tempROB=tempROB->pnext;
				while(tempROB)	//detect store dependancy
				{
					if(tempROB->stage>=tempRS1->stage && tempROB->speflag<5\
					  &&tempROB->speflag!=0&&tempROB->address==tempRS1->address\
						&&StoreLoad.address==tempRS1->address)
					{   
						i=1;
						break;
					}
					tempROB=tempROB->pnext;
				}
				if(!i)  //no date dependancy
					tempRS1->speflag=6;
				
			}
			
			//if no dependence, memory access
			if(tempRS1->speflag==6)  // for load memory access
			{
				tempRS1->desValue=dataSeg[(tempRS1->address-700)/4];
				tempRS1->speflag=7;
			}
			else
			{
				// broadcase load data to RS/ROB
				if(tempRS1->speflag==7) 
				{
					tempROB=ROBUnit.used;	//for load write result to ROB
					while(tempROB)
					{
						if(tempRS1->ID==tempROB->ID)
						{
							tempROB->desValue=tempRS1->desValue;
							tempROB->nextstate= 1;
							tempRS1->nextstate= 1;
							tempROB->stage=tempRS1->stage;
							tempROB->speflag=0;
							tempRS1->speflag=0;
							break;
						}
						tempROB=tempROB->pnext;
					}
					
					if(tempRS1->des>=0)
					{
						tempRS2=RSUnit.used;     //broadcast the ready result to other RS
						while(tempRS2)
						{
							if(tempRS2->stage==issue\
							||(tempRS2->stage==fetch && tempRS2->nextstate==1))
							{
								if(tempRS2->op1==tempRS1->des)
								{
									tempRS2->op1Value=tempRS1->desValue;
									tempRS2->ready--;
								}
								if(tempRS2->op2==tempRS1->des)
								{
									tempRS2->op2Value=tempRS1->desValue;
									tempRS2->ready--;
								}
							}
							tempRS2=tempRS2->pnext;
						}
					}

				}
			}
		}
		
		tempRS1=tempRS1->pnext;
	}

}


void CommitStage()
{
	ROB_E *tempROB=ROBUnit.used;
			
	if(tempROB)
	{
		while(tempROB->pnext)
			tempROB=tempROB->pnext;

		if(tempROB->stage==writeresult)
		{
			tempROB->nextstate=1;
			if(tempROB->speflag==2)
			{	
				memset(&StoreLoad,0,sizeof(ROB_E));
				memcpy(&StoreLoad, tempROB, sizeof(ROB_E));
				StoreLoad.speflag=3;
			}
		}		
	}
}

void Display()
{
	int i = 0;
	RS_E  *tempRS=RSUnit.used;
	ROB_E *tempROB=ROBUnit.used;
	IQ_E  *tempIQ=IQUnit.used;
	BTB_E *tempBTB=BTBUnit.used;
	
	if((cycle >= start && cycle <= end)||over)
	{
		 if(over)
		 	fprintf(poutput,"Final ");
		fprintf(poutput,"Cycle <%d>:\n",cycle);
		fprintf(poutput,"IQ:\n"); //print IQ 
		
		if(tempIQ)
		{	
			while(tempIQ->pnext)
				tempIQ=tempIQ->pnext;
			while(tempIQ)
			{	
				InterpretData(tempIQ->instruction, print,0, 0, 0); 
				tempIQ=tempIQ->pprev;
			}
		}

		fprintf(poutput,"RS:\n");  //print RS
		if(tempRS)
		{	
			while(tempRS->pnext)
				tempRS=tempRS->pnext;
			while(tempRS)
			{	
				InterpretData(tempRS->instruction, print,0, 0, 0); 
				tempRS=tempRS->pprev;
			}
		}

		fprintf(poutput,"ROB:\n");  //print ROB
		if(tempROB)
		{	
			while(tempROB->pnext)
				tempROB=tempROB->pnext;
			while(tempROB)
			{	
				InterpretData(tempROB->instruction, print,0, 0, 0); 
				tempROB=tempROB->pprev;
			}
		}

		fprintf(poutput,"BTB:\n");   //print BTB
		if(tempBTB)
		{
			while(tempBTB->pnext)
				tempBTB=tempBTB->pnext;
			while(tempBTB)
			{
				if(tempBTB->Predict!=2)
				{
					fprintf(poutput,"[Entry %d]:<%d,%d,%d>\n",tempBTB->ID,\
						tempBTB->CurrPC,tempBTB->TarPC,\
						tempBTB->Predict);
				}
				else
				{
					fprintf(poutput,"[Entry %d]:<%d,%d,NotSet>\n",tempBTB->ID,\
						tempBTB->CurrPC,tempBTB->TarPC);
				}
				tempBTB=tempBTB->pprev;
			}
		}
				
		fprintf(poutput,"Registers:\n");  //print Registers file
		fprintf(poutput,"R00:");
		for(i=0; i<8; i++)
			fprintf(poutput,"	%d",RFUnit[i].regValue);
		fprintf(poutput,"\nR08:");
		for(i=8; i<16; i++)
			fprintf(poutput,"	%d",RFUnit[i].regValue);
		fprintf(poutput,"\nR16:");
		for(i=16; i<24; i++)
			fprintf(poutput,"	%d",RFUnit[i].regValue);
		fprintf(poutput,"\nR24:");
		for(i=24; i<32; i++)
			fprintf(poutput,"	%d",RFUnit[i].regValue);
		fprintf(poutput,"\n");

		fprintf(poutput,"Data Segment:\n700:");	//print data segment
		for(i=0;i<cnt;i++)
			fprintf(poutput,"	%d",dataSeg[i]);
		fprintf(poutput,"\n");
	}
	
}


void ReleaseRS()
{
	RS_E  *tempRS=RSUnit.used;
	RS_E  *tempRS1=NULL;
	U8    i=0;

	while(tempRS)
	{
		i=0;
		if(tempRS->stage==writeresult)
		{
			if(tempRS->pprev)
				tempRS1=tempRS->pprev;
			else
				i=1;
			
			if(tempRS->pprev)
			{
				if(tempRS->pnext)
				{	
					tempRS->pprev->pnext= tempRS->pnext;
					tempRS->pnext->pprev= tempRS->pprev;
				}
				else
					tempRS->pprev->pnext=NULL;
			}
			else
			{
				if(tempRS->pnext)
				{
					RSUnit.used=tempRS->pnext;
					tempRS->pnext->pprev=NULL;
				}
				else
					RSUnit.used=NULL;
			}
			memset(tempRS,0,sizeof(RS_E));
			tempRS->pnext=RSUnit.idle;
			if(RSUnit.idle)
				RSUnit.idle->pprev=tempRS;
			RSUnit.idle=tempRS;

			if(!i)
				tempRS=tempRS1;
					
		}
		if(!i)
			tempRS=tempRS->pnext;
		else
			tempRS=RSUnit.used;
		
	}

}

void ReleaseROB()
{
	ROB_E *tempROB=ROBUnit.used;
	ROB_E *tempROB1=NULL;
	U8 	  i=0;

	while(tempROB)
	{
		i=0;
		if(tempROB->stage==commit)
		{				
			if(tempROB->pprev)
				tempROB1=tempROB->pprev;
			else
				i=1;

			if(tempROB->instruction==13)
				over=1;
			
			//update registers to idle
			if(tempROB->des<32 && tempROB->des>=0)
			{
				if(tempROB->speflag==0)
				{
					if(RFUnit[tempROB->des].flag)
					{
						RFUnit[tempROB->des].regValue = tempROB->desValue;
						RFUnit[tempROB->des].flag=0;
					}
				}
			}
			
			if(tempROB->pprev)
			{
				if(tempROB->pnext)
				{	
					tempROB->pprev->pnext= tempROB->pnext;
					tempROB->pnext->pprev= tempROB->pprev;
				}
				else
					tempROB->pprev->pnext=NULL;
			}
			else
			{
				if(tempROB->pnext)
				{
					ROBUnit.used=tempROB->pnext;
					tempROB->pnext->pprev=NULL;
				}
				else
					ROBUnit.used=NULL;
			}
			memset(tempROB,0,sizeof(ROB_E));
			tempROB->pnext=ROBUnit.idle;
			if(ROBUnit.idle)
				ROBUnit.idle->pprev=tempROB;
			ROBUnit.idle=tempROB;

			if(!i)
				tempROB=tempROB1;
			
		}
		if(!i)
			tempROB=tempROB->pnext;
		else
			tempROB=ROBUnit.used;
	}
	
	//update RF to busy
	tempROB=ROBUnit.used;
	while(tempROB)
	{
		if(tempROB->stage==issue)
		{
			if(tempROB->des<32 && tempROB->des>=0)
				if(!RFUnit[tempROB->des].flag)
					RFUnit[tempROB->des].flag=1;
		}
		tempROB=tempROB->pnext;
	}

}

void UpdateState()
{
	RS_E  *tempRS=RSUnit.used;
	ROB_E *tempROB=ROBUnit.used;
	BTB_E *tempBTB=BTBUnit.used;
	IQ_E  *tempIQ=IQUnit.used;
	RS_E  *tempRS1=NULL;
	ROB_E *tempROB1=NULL;
	U8	   result=0;
	
	while(tempRS)	// check RS and change state
	{
		if(tempRS->nextstate)
		{
			tempRS->stage++;
			tempRS->nextstate=0;
			
			if(tempRS->speflag==1 && tempRS->stage==execute) //for store
			{
				tempRS->stage=writeresult;
				tempROB = ROBUnit.used;
				while(tempROB)
				{
					if(tempROB->ID==tempRS->ID)
					{
						tempROB->stage=writeresult;
						tempROB->address=tempRS->address;
						tempROB->nextstate=0;
						tempROB->speflag=2;
						break;
					}
					tempROB=tempROB->pnext;
				}
			}		
			
			if(tempRS->speflag==10 && tempRS->stage==execute)  //for branch
			{
				tempRS->stage = writeresult;
				tempROB = ROBUnit.used;
				while(tempROB)
				{
					if(tempROB->ID==tempRS->ID)
					{
						tempROB->stage= writeresult;
						tempROB->nextstate=0;
						tempROB->desValue=tempRS->desValue;
						tempROB->CurrPC=tempRS->CurrPC;
						break;
					}
					tempROB=tempROB->pnext;
				}
				
				tempBTB=BTBUnit.used;
				while(tempBTB)
				{
					if(tempBTB->CurrPC==tempRS->CurrPC)					
					{		
						result=tempRS->desValue;
						
						//mispredict,flush pipeline
						if((tempBTB->Predict==1 && tempRS->desValue==0)||\
							((tempBTB->Predict==0||tempBTB->Predict==2)&&\
							tempRS->desValue==1))  
						{
							
							//flush RS
							if(tempRS->pprev)  
							{	
								tempRS1=tempRS->pprev;
								tempRS->pprev=NULL;
								RSUnit.used=tempRS;
								tempRS1->pnext=RSUnit.idle;
								if(RSUnit.idle)
									RSUnit.idle->pprev=tempRS1;
								while(tempRS1->pprev)
									tempRS1=tempRS1->pprev;
								RSUnit.idle=tempRS1;								
							}
							else RSUnit.used=tempRS;
							
							//flush ROB
							tempROB=ROBUnit.used;
							while(tempROB)
							{
								if(tempROB->CurrPC==tempBTB->CurrPC)
								{
									if(tempROB->pprev)
									{
										tempROB1=tempROB->pprev;
										tempROB->pprev=NULL;
										ROBUnit.used=tempROB;
										tempROB1->pnext=ROBUnit.idle;
										if(ROBUnit.idle)
											ROBUnit.idle->pprev=tempROB1;
										while(tempROB1->pprev)
											tempROB1=tempROB1->pprev;
										ROBUnit.idle=tempROB1;							
									}
									else ROBUnit.used=tempROB;
									
									break;
								}		
								tempROB=tempROB->pnext;
							}
							
							//flush IQ
							tempIQ=IQUnit.used;
							if(tempIQ)
							{
								while(tempIQ->pnext)
									tempIQ=tempIQ->pnext;
								tempIQ->pnext=IQUnit.idle;
								if(IQUnit.idle)
									IQUnit.idle->pprev=tempIQ;
								IQUnit.idle=IQUnit.used;
								IQUnit.used=NULL;
							}
							//update PC
							if((tempBTB->Predict==0||tempBTB->Predict==2)&&\
							tempRS->desValue==1) 
							{
								locate=tempBTB->TarPC;
								fseek(pinput,locate-584L,SEEK_SET);
							}
							if(tempBTB->Predict==1 && tempRS->desValue==0)
							{
								locate=tempBTB->CurrPC+4;
								fseek(pinput,locate-584L,SEEK_SET);
							}
														
						}
						//update BTB
						tempBTB->Predict=result;
						break;
					}		
					
					tempBTB=tempBTB->pnext;
				}	
	
			}

		}
		
		tempRS=tempRS->pnext;
	}
	
	tempROB=ROBUnit.used;
	while(tempROB)	// check ROB and change state
	{
		if(tempROB->nextstate)
		{
			tempROB->stage++;
			tempROB->nextstate=0;
		}
		tempROB=tempROB->pnext;
	}
	
}

void 	ChangeState()
{
	if(StoreLoad.speflag==4) //update data segment
	{
		dataSeg[(StoreLoad.address-700)/4]=RFUnit[StoreLoad.des].regValue;
		memset(&StoreLoad,0,sizeof(ROB_E));
	}
	if(StoreLoad.speflag==3)
		StoreLoad.speflag++;
	
	UpdateState();
	ReleaseRS();
	ReleaseROB();
}

void Process()
{
	
	while(cycle)
	{
		FetchStage();
		IssueStage();
		ExeStage();
		WriteStage();
		CommitStage();
		Display();
		ChangeState();
		if(cycle==end)
			return;
		if(over)
		{
			Display();
			return;
		}
		cycle++;
	}
	
}

int main(int argc, char** argv)
{
	if(argc < 3)  //estimate the num of arguments
	{
		perror("there are not enough arguments!\n");
		return -1;
	}

	if(argc == 3)
	{
		start = 0;
		end =0xffffffff;
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
	}
	
	if(argc==4)
	{
		char tem[8];
		int  i=2;
		memset(tem,0,8);
		while(argv[3][i]!=':')
		{
			tem[i-2]= argv[3][i];
			i++;
		}
		tem[i-2]='\0';
		start = atoi(tem);
		memset(tem,0,8);
		int j=0;
		i++;
		while(argv[3][i]!='\0')
		{
			tem[j] = argv[3][i];
			j++;
			i++;
		}
		tem[j]='\0';
		end = atoi(tem);
		//printf("start=%d, end=%d\n",start, end);
		
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
		
	}

	char temp=0;
	int  rv=0;
	fseek(pinput,700L-584L,SEEK_SET);
	do
	{
		rv=fread(&temp, 1, 1, pinput);
		if(rv)
			cnt++;
	}while(rv);
	cnt=cnt/4;
	//printf("cnt=%d\n",cnt);
	
	fseek(pinput,0L,SEEK_SET);	
	InitUnits();
	Process();
	
	fclose(pinput);
	fclose(poutput);
	return 0;

}
