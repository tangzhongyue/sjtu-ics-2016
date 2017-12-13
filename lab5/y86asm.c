#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "y86asm.h"

line_t *y86bin_listhead = NULL;   /* the head of y86 binary code line list*/
line_t *y86bin_listtail = NULL;   /* the tail of y86 binary code line list*/
int y86asm_lineno = 0; /* the current line number of y86 assemble code */

#define err_print(_s, _a ...) do { \
  if (y86asm_lineno < 0) \
    fprintf(stderr, "[--]: "_s"\n", ## _a); \
  else \
    fprintf(stderr, "[L%d]: "_s"\n", y86asm_lineno, ## _a); \
} while (0);

int vmaddr = 0;    /* vm addr */

/* register table */
reg_t reg_table[REG_CNT] = {
    {"%eax", REG_EAX},
    {"%ecx", REG_ECX},
    {"%edx", REG_EDX},
    {"%ebx", REG_EBX},
    {"%esp", REG_ESP},
    {"%ebp", REG_EBP},
    {"%esi", REG_ESI},
    {"%edi", REG_EDI},
};

regid_t find_register(char *name)
{
	int i;
	for(i=0;i<REG_CNT;i++){
		if(strcmp(name,reg_table[i].name)==0){
			return reg_table[i].id;
		}
	}
    return REG_ERR;
}

/* instruction set */
instr_t instr_set[] = {
    {"nop", 3,   HPACK(I_NOP, F_NONE), 1 },
    {"halt", 4,  HPACK(I_HALT, F_NONE), 1 },
    {"rrmovl", 6,HPACK(I_RRMOVL, F_NONE), 2 },
    {"cmovle", 6,HPACK(I_RRMOVL, C_LE), 2 },
    {"cmovl", 5, HPACK(I_RRMOVL, C_L), 2 },
    {"cmove", 5, HPACK(I_RRMOVL, C_E), 2 },
    {"cmovne", 6,HPACK(I_RRMOVL, C_NE), 2 },
    {"cmovge", 6,HPACK(I_RRMOVL, C_GE), 2 },
    {"cmovg", 5, HPACK(I_RRMOVL, C_G), 2 },
    {"irmovl", 6,HPACK(I_IRMOVL, F_NONE), 6 },
    {"rmmovl", 6,HPACK(I_RMMOVL, F_NONE), 6 },
    {"mrmovl", 6,HPACK(I_MRMOVL, F_NONE), 6 },
    {"addl", 4,  HPACK(I_ALU, A_ADD), 2 },
    {"subl", 4,  HPACK(I_ALU, A_SUB), 2 },
    {"andl", 4,  HPACK(I_ALU, A_AND), 2 },
    {"xorl", 4,  HPACK(I_ALU, A_XOR), 2 },
    {"jmp", 3,   HPACK(I_JMP, C_YES), 5 },
    {"jle", 3,   HPACK(I_JMP, C_LE), 5 },
    {"jl", 2,    HPACK(I_JMP, C_L), 5 },
    {"je", 2,    HPACK(I_JMP, C_E), 5 },
    {"jne", 3,   HPACK(I_JMP, C_NE), 5 },
    {"jge", 3,   HPACK(I_JMP, C_GE), 5 },
    {"jg", 2,    HPACK(I_JMP, C_G), 5 },
    {"call", 4,  HPACK(I_CALL, F_NONE), 5 },
    {"ret", 3,   HPACK(I_RET, F_NONE), 1 },
    {"pushl", 5, HPACK(I_PUSHL, F_NONE), 2 },
    {"popl", 4,  HPACK(I_POPL, F_NONE),  2 },
    {".byte", 5, HPACK(I_DIRECTIVE, D_DATA), 1 },
    {".word", 5, HPACK(I_DIRECTIVE, D_DATA), 2 },
    {".long", 5, HPACK(I_DIRECTIVE, D_DATA), 4 },
    {".pos", 4,  HPACK(I_DIRECTIVE, D_POS), 0 },
    {".align", 6,HPACK(I_DIRECTIVE, D_ALIGN), 0 },
    {NULL, 1,    0   , 0 } //end
};

instr_t *find_instr(char *name)
{
    instr_t *tmp=&instr_set[0];
    while(!(tmp->name==NULL)){
    	if(strcmp(tmp->name,name)==0){
    		return tmp;
    	}
    	tmp++;
    }
    return NULL;
}

/* symbol table (don't forget to init and finit it) */
symbol_t *symtab = NULL;

/*
 * find_symbol: scan table to find the symbol
 * args
 *     name: the name of symbol
 *
 * return
 *     symbol_t: the 'name' symbol
 *     NULL: not exist
 */
symbol_t *find_symbol(char *name)
{
    symbol_t *tmp=NULL;
    tmp=symtab->next;
    while(tmp){
    	if(strncmp(tmp->name,name,strlen(name))==0){
    		return tmp;
    	}
    	tmp=tmp->next;
    }
    return NULL;
}

/*
 * add_symbol: add a new symbol to the symbol table
 * args
 *     name: the name of symbol
 *
 * return
 *     0: success
 *     -1: error, the symbol has exist
 */
int add_symbol(char *name)
{    
    /* check duplicate */
	if(find_symbol(name)!=NULL){
		err_print("Dup symbol:%s", name);
		return -1;
	}
    /* create new symbol_t (don't forget to free it)*/
    /* add the new symbol_t to symbol table */
    symbol_t *stmp=NULL,*sptr=symtab;
    stmp=(symbol_t *)malloc(sizeof(symbol_t));
    memset(stmp,0,sizeof(symbol_t));
	stmp->name=(char *)malloc(strlen(name));
	strcpy(stmp->name,name);
	stmp->next=NULL;
	stmp->addr=y86bin_listtail->y86bin.addr;
    while(sptr->next!=NULL){
    	sptr=sptr->next;
    }
    sptr->next=stmp;
    return 0;
}

/* relocation table (don't forget to init and finit it) */
reloc_t *reltab = NULL;

/*
 * add_reloc: add a new relocation to the relocation table
 * args
 *     name: the name of symbol
 *     bin: the y86bin of the line the relocation located in
 */
void add_reloc(char *name, bin_t *bin)
{
    /* create new reloc_t (don't forget to free it)*/
    /* add the new reloc_t to relocation table */
    reloc_t* rtmp=NULL,*rptr=reltab;
    rtmp=(reloc_t *)malloc(sizeof(reloc_t));
    memset(rtmp,0,sizeof(reloc_t));
    rtmp->next=NULL;
   	rtmp->name=(char *)malloc(strlen(name));
	strcpy(rtmp->name,name);
	rtmp->y86bin=bin;
    while(rptr->next!=NULL){
    	rptr=rptr->next;
    }
    rptr->next=rtmp;
}


/* macro for parsing y86 assembly code */
#define IS_DIGIT(s) ((*(s)>='0' && *(s)<='9') || *(s)=='-' || *(s)=='+')
#define IS_LETTER(s) ((*(s)>='a' && *(s)<='z') || (*(s)>='A' && *(s)<='Z'))
#define IS_COMMENT(s) (*(s)=='#')
#define IS_REG(s) (*(s)=='%')
#define IS_IMM(s) (*(s)=='$')

#define IS_BLANK(s) (*(s)==' ' || *(s)=='\t')
#define IS_END(s) (*(s)=='\0')

#define SKIP_BLANK(s) do {  \
  while(!IS_END(s) && IS_BLANK(s))  \
    (s)++;    \
} while(0);

/* return value from different parse_xxx function */
typedef enum { PARSE_ERR=-1, PARSE_REG, PARSE_DIGIT, PARSE_SYMBOL, 
    PARSE_MEM, PARSE_DELIM, PARSE_INSTR, PARSE_LABEL} parse_t;

/*
 * parse_instr: parse an expected data token (e.g., 'rrmovl')
 * args
 *     ptr: point to the start of string
 *     inst: point to the inst_t within instr_set
 *
 * return
 *     PARSE_INSTR: success, move 'ptr' to the first char after token,
 *                            and store the pointer of the instruction to 'inst'
 *     PARSE_ERR: error, the value of 'ptr' and 'inst' are undefined
 */
parse_t parse_instr(char **ptr, instr_t **inst)
{
    /* skip the blank */
	SKIP_BLANK(*ptr);
    /* find_instr and check end */
	if(IS_END(*ptr))return PARSE_ERR;
    /* set 'ptr' and 'inst' */
    int length=0;
    while(!IS_BLANK(*ptr+length)&&!IS_END(*ptr+length)){
    	length+=1;
    }
	char code[length+1];
	strncpy(code,ptr[0],length);
	code[length]='\0';
	*inst=find_instr(code);
	if(*inst==NULL){
    	return PARSE_ERR;
    }
    *ptr=*ptr+length;
    return PARSE_INSTR;
}

/*
 * parse_delim: parse an expected delimiter token (e.g., ',')
 * args
 *     ptr: point to the start of string
 *
 * return
 *     PARSE_DELIM: success, move 'ptr' to the first char after token
 *     PARSE_ERR: error, the value of 'ptr' and 'delim' are undefined
 */
parse_t parse_delim(char **ptr, char delim)
{
    /* skip the blank and check */
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr))return PARSE_ERR;
	if(**ptr!=delim)return PARSE_ERR;
    /* set 'ptr' */
	*ptr=*ptr+1;
	return PARSE_DELIM;
}

/*
 * parse_reg: parse an expected register token (e.g., '%eax')
 * args
 *     ptr: point to the start of string
 *     regid: point to the regid of register
 *
 * return
 *     PARSE_REG: success, move 'ptr' to the first char after token, 
 *                         and store the regid to 'regid'
 *     PARSE_ERR: error, the value of 'ptr' and 'regid' are undefined
 */
parse_t parse_reg(char **ptr, regid_t *regid)
{
    /* skip the blank and check */
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr))return PARSE_ERR;
    /* find register */
	char reg[5];
	strncpy(reg,*ptr,4);
	reg[4]='\0';
	*regid=find_register(reg);
	if(*regid==REG_ERR)return PARSE_ERR;
    /* set 'ptr' and 'regid' */
	*ptr=*ptr+4;
    return PARSE_REG;
}

/*
 * parse_symbol: parse an expected symbol token (e.g., 'Main')
 * args
 *     ptr: point to the start of string
 *     name: point to the name of symbol (should be allocated in this function)
 *
 * return
 *     PARSE_SYMBOL: success, move 'ptr' to the first char after token,
 *                               and allocate and store name to 'name'
 *     PARSE_ERR: error, the value of 'ptr' and 'name' are undefined
 */
parse_t parse_symbol(char **ptr, char **name)
{
    /* skip the blank and check */
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr))return PARSE_ERR;
    /* allocate name and copy to it */
	int length=0;
	while(IS_LETTER(*ptr+length)||IS_DIGIT(*ptr+length)){
		length+=1;
	}
	*name=(char *)malloc(length);
	strncpy(*name,*ptr,length);
	*ptr=*ptr+length;
    /* set 'ptr' and 'name' */
	return PARSE_SYMBOL;
}

/*
 * parse_digit: parse an expected digit token (e.g., '0x100')
 * args
 *     ptr: point to the start of string
 *     value: point to the value of digit
 *
 * return
 *     PARSE_DIGIT: success, move 'ptr' to the first char after token
 *                            and store the value of digit to 'value'
 *     PARSE_ERR: error, the value of 'ptr' and 'value' are undefined
 */
parse_t parse_digit(char **ptr, long *value)
{
    /* skip the blank and check */
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr))return PARSE_ERR;
    /* calculate the digit, (NOTE: see strtoll()) */
	int length=0;int i=0;
	if(IS_END(*ptr+1)){
		if(**ptr>='0'&&**ptr<='9'){
			*value=**ptr-'0';
			*ptr=*ptr+1;
			return PARSE_DIGIT;
		}
	}
	else {
		if(ptr[0][0]=='0'&&ptr[0][1]=='x'){
			i=2;
			while((ptr[0][i]<='9'&&ptr[0][i]>='0')||(ptr[0][i]>='a'&&ptr[0][i]<='f')||(ptr[0][i]>='A'&&ptr[0][i]<='F')){
				length+=1;i+=1;
			}
			if(length==0)return PARSE_ERR;
			ptr[1]=*ptr+i+2;
			*value=strtoul(*ptr,ptr+1,16);
			*ptr=ptr[1];
			return PARSE_DIGIT;
		}
		else {
			int i=0;
			while(IS_DIGIT(*ptr+length)){
				length+=1;i+=1;
			}
			if(length==0)return PARSE_ERR;
			ptr[1]=*ptr+length;
			*value=strtol(*ptr,ptr+1,10);
			*ptr=*ptr+length;
			return PARSE_DIGIT;
		}
	}
    /* set 'ptr' and 'value' */
	
    return PARSE_ERR;
}

/*
 * parse_imm: parse an expected immediate token (e.g., '$0x100' or 'STACK')
 * args
 *     ptr: point to the start of string
 *     name: point to the name of symbol (should be allocated in this function)
 *     value: point to the value of digit
 *
 * return
 *     PARSE_DIGIT: success, the immediate token is a digit,
 *                            move 'ptr' to the first char after token,
 *                            and store the value of digit to 'value'
 *     PARSE_SYMBOL: success, the immediate token is a symbol,
 *                            move 'ptr' to the first char after token,
 *                            and allocate and store name to 'name' 
 *     PARSE_ERR: error, the value of 'ptr', 'name' and 'value' are undefined
 */
parse_t parse_imm(char **ptr, char **name, long *value)
{
    /* skip the blank and check */
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr))return PARSE_ERR;
    /* if IS_IMM, then parse the digit */
	if(IS_IMM(*ptr)){
		*ptr=*ptr+1;
		free(name);name=NULL;
		return parse_digit(ptr,value);
	}
    /* if IS_LETTER, then parse the symbol */
    if(IS_LETTER(*ptr)){
    	free(value);value=NULL;
    	return parse_symbol(ptr,name);
    }
    /* set 'ptr' and 'name' or 'value' */
    return PARSE_ERR;
}

/*
 * parse_mem: parse an expected memory token (e.g., '8(%ebp)')
 * args
 *     ptr: point to the start of string
 *     value: point to the value of digit
 *     regid: point to the regid of register
 *
 * return
 *     PARSE_MEM: success, move 'ptr' to the first char after token,
 *                          and store the value of digit to 'value',
 *                          and store the regid to 'regid'
 *     PARSE_ERR: error, the value of 'ptr', 'value' and 'regid' are undefined
 */
parse_t parse_mem(char **ptr, long *value, regid_t *regid)
{
    /* skip the blank and check */
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr))return PARSE_ERR;
    /* calculate the digit and register, (ex: (%ebp) or 8(%ebp)) */
    if(parse_delim(ptr,'(')==PARSE_DELIM){
    	if(parse_reg(ptr,regid)==PARSE_ERR){return PARSE_ERR;}
    	else {
    		if(parse_delim(ptr,')')==PARSE_ERR){return PARSE_ERR;}
    		return PARSE_MEM;
    	}
    }
	if(parse_digit(ptr,value)==PARSE_ERR){
		return PARSE_ERR;
	}
	else{
		if(parse_delim(ptr,'(')==PARSE_ERR)return PARSE_ERR;
		else{
    		if(parse_reg(ptr,regid)==PARSE_ERR){
    			return PARSE_ERR;
    		}
    		else {
    			if(parse_delim(ptr,')')==PARSE_ERR){\
    				return PARSE_ERR;
    			}
    			return PARSE_MEM;
    		}
		}
	}
    /* set 'ptr', 'value' and 'regid' */
    return PARSE_ERR;
}

/*
 * parse_data: parse an expected data token (e.g., '0x100' or 'array')
 * args
 *     ptr: point to the start of string
 *     name: point to the name of symbol (should be allocated in this function)
 *     value: point to the value of digit
 *
 * return
 *     PARSE_DIGIT: success, data token is a digit,
 *                            and move 'ptr' to the first char after token,
 *                            and store the value of digit to 'value'
 *     PARSE_SYMBOL: success, data token is a symbol,
 *                            and move 'ptr' to the first char after token,
 *                            and allocate and store name to 'name' 
 *     PARSE_ERR: error, the value of 'ptr', 'name' and 'value' are undefined
 */
parse_t parse_data(char **ptr, char **name, long *value)
{
    /* skip the blank and check */
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr))return PARSE_ERR;
    /* if IS_DIGIT, then parse the digit */
	if(IS_DIGIT(*ptr)){free(name);name=NULL;return parse_digit(ptr,value);}
    /* if IS_LETTER, then parse the symbol */
	if(IS_LETTER(*ptr)){free(value);value=NULL;return parse_symbol(ptr,name);}
    /* set 'ptr', 'name' and 'value' */
    return PARSE_ERR;
}

/*
 * parse_label: parse an expected label token (e.g., 'Loop:')
 * args
 *     ptr: point to the start of string
 *     name: point to the name of symbol (should be allocated in this function)
 *
 * return
 *     PARSE_LABEL: success, move 'ptr' to the first char after token
 *                            and allocate and store name to 'name'
 *     PARSE_ERR: error, the value of 'ptr' is undefined
 */
parse_t parse_label(char **ptr, char **name)
{
    /* skip the blank and check */
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr))return PARSE_ERR;
    /* allocate name and copy to it */
	int length=0;
	while(IS_LETTER(*ptr+length)||IS_DIGIT(*ptr+length)){
		length+=1;
	}
	*name=(char *)malloc(length+1);
	strncpy(*name,*ptr,length);
	name[0][length]='\0';
	*ptr=*ptr+length;
	if(parse_delim(ptr,':')==PARSE_ERR)return PARSE_ERR;
    /* set 'ptr' and 'name' */
    return PARSE_LABEL;
}

/*
 * parse_line: parse a line of y86 code (e.g., 'Loop: mrmovl (%ecx), %esi')
 * (you could combine above parse_xxx functions to do it)
 * args
 *     line: point to a line_t data with a line of y86 assembly code
 *
 * return
 *     PARSE_XXX: success, fill line_t with assembled y86 code
 *     PARSE_ERR: error, try to print err information (e.g., instr type and line number)
 */
type_t parse_line(line_t *line)
{

/* when finish parse an instruction or lable, we still need to continue check 
* e.g., 
*  Loop: mrmovl (%ebp), %ecx
*           call SUM  #invoke SUM function */
	
    /* skip blank and check IS_END */
    char **ptr=(char**)malloc(sizeof(char *));int i;
    (*ptr)=line->y86asm;
	SKIP_BLANK(*ptr);
	if(IS_END(*ptr)){
		free(ptr);return TYPE_COMM;
	}
    /* is a comment ? */
	if(**ptr=='#'){line->y86bin.bytes=0;return TYPE_COMM;}
    /* is a label ? */
   	char **name=(char **)malloc(sizeof(char*));
   	parse_t forlabel=PARSE_ERR,forimm=PARSE_ERR,fordata=PARSE_ERR;
   	forlabel=parse_label(ptr,name);
	if(forlabel==PARSE_LABEL){
		if(add_symbol(*name)==-1){
			line->type = TYPE_ERR;
			free(*name);
			free(name);free(ptr);
    		return line->type;
		}
		else{
			line->type=TYPE_INS;
			SKIP_BLANK(*ptr);
			if(IS_END(*ptr)||parse_delim(ptr,'#')==PARSE_DELIM){
				free(*name);
				free(name);free(ptr);
				return line->type;
			}
		}
	}
    /* is an instruction ? */
	if(forlabel==PARSE_ERR){
		(*ptr)=line->y86asm;
	}
	instr_t **itmp=(instr_t **)malloc(sizeof(instr_t*));
	regid_t *regtmp=(regid_t *)malloc(sizeof(regid_t));
	long* value=(long *)malloc(sizeof(long));int ali;
	if(parse_instr(ptr,itmp)==PARSE_ERR){
		line->type=TYPE_ERR;
		return line->type;
	}
	else{
		/* set type and y86bin */
    	/* update vmaddr */  
    	/* parse the rest of instruction according to the itype */
    	char icode=HIGH((*itmp)->code);
    	char ifun=LOW((*itmp)->code);
    	if(icode<0xc)line->y86bin.codes[0]=(*itmp)->code;
		switch(icode){
			case(0x0):case(0x1):case(0x9):
			//halt,nop,ret
			line->y86bin.bytes=1;line->type=TYPE_INS;break;
			case(0x2):case(0x6):
			//rrmovl,alu
			if(parse_reg(ptr,regtmp)==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid REG");free(regtmp);break;
			}
			if(parse_delim(ptr,',')==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid ','");break;
			}
			if(parse_reg(ptr,&regtmp[1])==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid REG");free(regtmp);break;
			}
			line->y86bin.codes[1]=HPACK(regtmp[0],regtmp[1]);
			line->y86bin.bytes=2;
			line->type=TYPE_INS;
			free(regtmp);
			break;
			case(0x3):
			//irmovl
			line->y86bin.bytes=2;
			forimm=parse_imm(ptr,name,value);
			if(forimm==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid Immediate");break;
			}
			if(parse_delim(ptr,',')==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid ','");break;
			}
			if(parse_reg(ptr,regtmp)==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid REG");free(regtmp);break;
			}
			line->y86bin.codes[1]=HPACK(0xF,*regtmp);
			if(forimm==PARSE_SYMBOL){;
				symbol_t *symtmp=find_symbol(*name);
				if(symtmp==NULL){
					add_reloc(*name,&line->y86bin);
				}
				else{
					for(i=2;i<6;i++){
						line->y86bin.codes[i]=*((char *)&(symtmp->addr)+i-2);
					}
				}
				free(*name);free(name);
			}
			else{
				for(i=2;i<6;i++){
					line->y86bin.codes[i]=*((char *)value+i-2);
				}
				free(value);
			}
			line->type=TYPE_INS;line->y86bin.bytes=6;
			free(regtmp);
			break;
			case(0x4):
			//rmmovl
			line->y86bin.bytes=2;
			if(parse_reg(ptr,regtmp)==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid REG");free(regtmp);break;
			}
			if(parse_delim(ptr,',')==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid ','");break;
			}
			if(parse_mem(ptr,value,&regtmp[1])==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid MEM");free(regtmp);break;
			}
			line->y86bin.codes[1]=HPACK(regtmp[0],regtmp[1]);
			for(i=2;i<6;i++){
				line->y86bin.codes[i]=*((char *)value+i-2);
			}
			line->type=TYPE_INS;line->y86bin.bytes=6;
			free(regtmp);
			break;
			case(0x5):
			//mrmovl
			line->y86bin.bytes=2;
			if(parse_mem(ptr,value,&regtmp[1])==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid MEM");free(regtmp);break;
			}
			if(parse_delim(ptr,',')==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid ','");break;
			}
			if(parse_reg(ptr,regtmp)==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid REG");free(regtmp);break;
			}
			line->y86bin.codes[1]=HPACK(regtmp[0],regtmp[1]);
			for(i=2;i<6;i++){
				line->y86bin.codes[i]=*((char *)value+i-2);
			}
			line->type=TYPE_INS;line->y86bin.bytes=6;
			free(regtmp);free(value);
			break;
			case(0x7):case(0x8):
			//jmp,call
			line->y86bin.bytes=1;
			forimm=parse_imm(ptr,name,value);
			if(forimm==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid DEST");break;
			}
			if(forimm==PARSE_SYMBOL){
				symbol_t *symtmp=find_symbol(*name);
				if(symtmp==NULL){
					add_reloc(*name,&line->y86bin);
				}
				else{
					for(i=1;i<5;i++){
						line->y86bin.codes[i]=*((char *)(&symtmp->addr)+i-1);
					}
				}
				free(*name);free(name);
			}
			else{
				for(i=1;i<5;i++){
					line->y86bin.codes[i]=*((char *)value+i-1);
				}
				free(value);
			}
			line->type=TYPE_INS;line->y86bin.bytes=5;
			break;
			case(0xa):case(0xb):
			//pushl,popl
			if(parse_reg(ptr,regtmp)==PARSE_ERR){
				line->type=TYPE_ERR;err_print("Invalid REG");free(regtmp);break;
			}
			line->y86bin.codes[1]=HPACK(regtmp[0],0xF);
			line->type=TYPE_INS;
			line->y86bin.bytes=2;
			free(regtmp);
			break;
			case(0xc):
			//directive
			if(ifun==0){
				//.byte,.word,.long
				line->y86bin.codes[0]=HPACK(0xc,0);
				line->y86bin.bytes=0;
				fordata=parse_data(ptr,name,value);
				if(fordata==PARSE_ERR){
					line->type=TYPE_ERR;break;
				}
				int length=(*itmp)->bytes;
				if(fordata==PARSE_SYMBOL){
					symbol_t *symtmp=find_symbol(*name);
					if(symtmp==NULL){
						add_reloc(*name,&line->y86bin);
					}
					else{
						for(i=0;i<length;i++){
							line->y86bin.codes[i]=*((char *)&(symtmp->addr)+i);
						}
					}
					free(*name);free(name);
				}
				else{
					for(i=0;i<length;i++){
						line->y86bin.codes[i]=*((char *)value+i);
					}
					free(value);
				}
				line->y86bin.bytes=length;
			}
			else if(ifun==1){
				//.pos
				line->y86bin.bytes=0;
				if(parse_digit(ptr,value)==PARSE_ERR){
					line->type=TYPE_ERR;break;
				}
				if(*value<0){
					line->type=TYPE_ERR;break;
				}
				line->y86bin.addr=(*value);
				free(value);
			}
			else if(ifun==2){
				//.align
				line->y86bin.bytes=0;
				if(parse_digit(ptr,value)==PARSE_ERR){
					line->type=TYPE_ERR;break;
				}
				if(*value<0){
					line->type=TYPE_ERR;break;
				}
				ali=(*value);
				if(line->y86bin.addr%ali!=0){
					line->y86bin.addr=(line->y86bin.addr/ali)*ali+ali;
				}
				free(value);
			}
			line->type=TYPE_INS;
			break;
			default:break;
		}
	}  
	SKIP_BLANK(*ptr);
	if(!IS_END(*ptr)&&line->type!=TYPE_ERR){
		if(parse_delim(ptr,'#')==PARSE_ERR){
			line->type=TYPE_ERR;
		}
	}
	free(ptr);
    return line->type;
}

/*
 * assemble: assemble an y86 file (e.g., 'asum.ys')
 * args
 *     in: point to input file (an y86 assembly file)
 *
 * return
 *     0: success, assmble the y86 file to a list of line_t
 *     -1: error, try to print err information (e.g., instr type and line number)
 */
int assemble(FILE *in)
{
    static char asm_buf[MAX_INSLEN]; /* the current line of asm code */
    line_t *line;
    int slen;
    char *y86asm;
    /* read y86 code line-by-line, and parse them to generate raw y86 binary code list */
    while (fgets(asm_buf, MAX_INSLEN, in) != NULL) {
        slen  = strlen(asm_buf);
        if ((asm_buf[slen-1] == '\n') || (asm_buf[slen-1] == '\r')) { 
            asm_buf[--slen] = '\0'; /* replace terminator */
        }

        /* store y86 assembly code */
        y86asm = (char *)malloc(sizeof(char) * (slen + 1)); // free in finit
        strcpy(y86asm, asm_buf);

        line = (line_t *)malloc(sizeof(line_t)); // free in finit
        memset(line, '\0', sizeof(line_t));

        /* set defualt */
        line->type = TYPE_COMM;
        line->y86asm = y86asm;
        line->next = NULL;

        /* add to y86 binary code list */
        y86bin_listtail->next = line;
        line->y86bin.addr=y86bin_listtail->y86bin.addr+y86bin_listtail->y86bin.bytes;
        y86bin_listtail = line;
        y86asm_lineno ++;

        /* parse */
        if (parse_line(line) == TYPE_ERR)
            return -1;
    }
    /* skip line number information in err_print() */
    y86asm_lineno = -1;
    return 0;
}

/*
 * relocate: relocate the raw y86 binary code with symbol address
 *
 * return
 *     0: success
 *     -1: error, try to print err information (e.g., addr and symbol)
 */
int relocate(void)
{
	int i;
    reloc_t *rtmp = NULL;
    symbol_t *stmp=NULL;
    rtmp = reltab->next;
    char c;
    char addr[4];
    long value;
    while (rtmp) {
        /* find symbol */
    	stmp=find_symbol(rtmp->name);
    	if(stmp==NULL){
    		err_print("Unknown symbol:'%s'",rtmp->name);
    		return -1;
    	}
    	value=stmp->addr;
    	for(i=0;i<4;i++){
    		addr[i]=*((char *)&value+i);
    	}
        /* relocate y86bin according itype */
    	c=HIGH(rtmp->y86bin->codes[0]);
    	switch(c){
    		case 3:case 4:case 5:
    		for(i=0;i<4;i++){
    			rtmp->y86bin->codes[i+2]=addr[i];
    		}
    		break;
    		case 7:case 8:
    		for(i=0;i<4;i++){
    			rtmp->y86bin->codes[i+1]=addr[i];
    		}
    		break;
    		case (0xc):
    		for(i=0;i<rtmp->y86bin->bytes;i++){
    			rtmp->y86bin->codes[i]=addr[i];
    		}
    		break;
    		default:break;
    	}
        /* next */
        rtmp = rtmp->next;
    }
    return 0;
}

/*
 * binfile: generate the y86 binary file
 * args
 *     out: point to output file (an y86 binary file)
 *
 * return
 *     0: success
 *     -1: error
 */
int binfile(FILE *out)
{
    /* prepare image with y86 binary code */
	line_t* tmp=y86bin_listhead->next;
	int size=y86bin_listtail->y86bin.addr+y86bin_listtail->y86bin.bytes;
	int end=0;
	/* binary write y86 code to output file (NOTE: see fwrite()) */
	while(tmp){
		if(tmp->y86bin.addr<=size)
		{	
			if(end<tmp->y86bin.addr){
				fseek(out,tmp->y86bin.addr-end,1);
			}
			fwrite(tmp->y86bin.codes,tmp->y86bin.bytes,1,out);
			end=tmp->y86bin.addr+tmp->y86bin.bytes;
		}
		tmp=tmp->next;
	}
    return 0;
}


/* whether print the readable output to screen or not ? */
bool_t screen = FALSE; 

static void hexstuff(char *dest, int value, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        char c;
        int h = (value >> 4*i) & 0xF;
        c = h < 10 ? h + '0' : h - 10 + 'a';
        dest[len-i-1] = c;
    }
}

void print_line(line_t *line)
{
    char buf[26];

    /* line format: 0xHHH: cccccccccccc | <line> */
    if (line->type == TYPE_INS) {
        bin_t *y86bin = &line->y86bin;
        int i;
        
        strcpy(buf, "  0x000:              | ");
        
        hexstuff(buf+4, y86bin->addr, 3);
        if (y86bin->bytes > 0)
            for (i = 0; i < y86bin->bytes; i++)
                hexstuff(buf+9+2*i, y86bin->codes[i]&0xFF, 2);
    } else {
        strcpy(buf, "                      | ");
    }

    printf("%s%s\n", buf, line->y86asm);
}

/* 
 * print_screen: dump readable binary and assembly code to screen
 * (e.g., Figure 4.8 in ICS book)
 */
void print_screen(void)
{
    line_t *tmp = y86bin_listhead->next;
    
    /* line by line */
    while (tmp != NULL) {
        print_line(tmp);
        tmp = tmp->next;
    }
}

/* init and finit */
void init(void)
{
    reltab = (reloc_t *)malloc(sizeof(reloc_t)); // free in finit
    memset(reltab, 0, sizeof(reloc_t));

    symtab = (symbol_t *)malloc(sizeof(symbol_t)); // free in finit
    memset(symtab, 0, sizeof(symbol_t));

    y86bin_listhead = (line_t *)malloc(sizeof(line_t)); // free in finit
    memset(y86bin_listhead, 0, sizeof(line_t));
    y86bin_listtail = y86bin_listhead;
    y86bin_listtail->y86bin.bytes=0;y86bin_listtail->y86bin.addr=0;
    y86asm_lineno = 0;
}

void finit(void)
{
    reloc_t *rtmp = NULL;
    do {
        rtmp = reltab->next;
        if (reltab->name) 
            free(reltab->name);

        free(reltab);
        reltab = rtmp;
    } while (reltab);
    
    symbol_t *stmp = NULL;
    do {
        stmp = symtab->next;
        if (symtab->name) 

            free(symtab->name);

        free(symtab);
        symtab = stmp;
    } while (symtab);

    line_t *ltmp = NULL;
    do {
        ltmp = y86bin_listhead->next;
        if (y86bin_listhead->y86asm) 
        {

            free(y86bin_listhead->y86asm);
        }

        free(y86bin_listhead);
        y86bin_listhead = ltmp;
    } while (y86bin_listhead);
}

static void usage(char *pname)
{
    printf("Usage: %s [-v] file.ys\n", pname);
    printf("   -v print the readable output to screen\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    int rootlen;
    char infname[512];
    char outfname[512];
    int nextarg = 1;
    FILE *in = NULL, *out = NULL;
    
    if (argc < 2)
        usage(argv[0]);
    
    if (argv[nextarg][0] == '-') {
        char flag = argv[nextarg][1];
        switch (flag) {
          case 'v':
            screen = TRUE;
            nextarg++;
            break;
          default:
            usage(argv[0]);
        }
    }

    /* parse input file name */
    rootlen = strlen(argv[nextarg])-3;
    /* only support the .ys file */
    if (strcmp(argv[nextarg]+rootlen, ".ys"))
        usage(argv[0]);
    
    if (rootlen > 500) {
        err_print("File name too long");
        exit(1);
    }


    /* init */
    init();

    
    /* assemble .ys file */
    strncpy(infname, argv[nextarg], rootlen);
    strcpy(infname+rootlen, ".ys");
    in = fopen(infname, "r");
    if (!in) {
        err_print("Can't open input file '%s'", infname);
        exit(1);
    }
    
    if (assemble(in) < 0) {
        err_print("Assemble y86 code error");
        fclose(in);
        exit(1);
    }
    fclose(in);


    /* relocate binary code */
    if (relocate() < 0) {
        err_print("Relocate binary code error");
        exit(1);
    }


    /* generate .bin file */
    strncpy(outfname, argv[nextarg], rootlen);
    strcpy(outfname+rootlen, ".bin");
    out = fopen(outfname, "wb");
    if (!out) {
        err_print("Can't open output file '%s'", outfname);
        exit(1);
    }

    if (binfile(out) < 0) {
        err_print("Generate binary file error");
        fclose(out);
        exit(1);
    }
    fclose(out);
    
    /* print to screen (.yo file) */
    if (screen)
       print_screen(); 

    /* finit */
    finit();
    return 0;
}


