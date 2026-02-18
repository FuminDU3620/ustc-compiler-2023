%option noyywrap
%{
/*****************声明和选项设置  begin*****************/
#include <stdio.h>
#include <stdlib.h>

#include "syntax_tree.h"
#include "syntax_analyzer.h"

int lines = 1;
int pos_start = 1;
int pos_end = 1;

void pass_node(char *text){
     yylval.node = new_syntax_tree_node(text);
}

/*****************声明和选项设置  end*****************/

%}


%%
 /* to do for students */
 /* two cases for you, pass_node will send flex's token to bison */
 
"/*"([^*]|\*+[^*/])*"*/"       { pos_start = pos_end; pos_end += strlen(yytext);}

[ \t]     { pos_start = pos_end; pos_end += strlen(yytext); }

else      { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return SPECELSE;}
if        { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return SPECIF;}
int       { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return SPECINT;}
return    { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return SPECRETURN;}
void      { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return SPECVOID;}
while     { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return SPECWHILE;}
float     { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return SPECFLOAT;}
"+"  	{ pos_start = pos_end; pos_end += 1; pass_node(yytext); return ADD;}
"-"  	{ pos_start = pos_end; pos_end += 1; pass_node(yytext); return SUB;}
"*"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return MUL;}
"/"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return DIV;}
"<="      { pos_start = pos_end; pos_end += 2; pass_node(yytext); return LE;}
">="      { pos_start = pos_end; pos_end += 2; pass_node(yytext); return GE;}
"<"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return LESS;}
">"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return GREAT;}
"=="      { pos_start = pos_end; pos_end += 2; pass_node(yytext); return EQUAL;}
"!="      { pos_start = pos_end; pos_end += 2; pass_node(yytext); return NEQUAL;}
"="       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return ASSIGN;}
";"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return SEMICOLON;}
","       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return COMMA;}
"("       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return LPAREN;}
")"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return RPAREN;}
"["       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return LSQUARE;}
"]"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return RSQUARE;}
"{"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return LCURLY;}
"}"       { pos_start = pos_end; pos_end += 1; pass_node(yytext); return RCURLY;}

[a-zA-Z]+                          { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return ID;}
[0-9]+                             { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return INTEGER;}
[0-9]+\.[0-9]*|[0-9]*\.[0-9]+      { pos_start = pos_end; pos_end += strlen(yytext); pass_node(yytext); return FLOAT;}
\r\n|\n|\r                         { pos_end = 1; lines += 1;}

.                                  { pos_start = pos_end; pos_end++; return ERROR; }

 /****请在此补全所有flex的模式与动作  end******/
%%
