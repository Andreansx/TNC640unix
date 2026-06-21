#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK16InsertRowsStatus6amountEv(void*);
extern int _ZNK16InsertRowsStatus8rowIndexEv(void*);
extern int _ZNK15PasteRowsStatus3rowEv(void*);
extern int _ZNK15PasteRowsStatus6amountEv(void*);
extern int _ZNK17SubtreeProxyModel9rootIndexEv(void*);
extern int _ZThn8_NK24ColumnFilteredTableModel10columnDataEv(void*);
extern int _ZNK24ColumnFilteredTableModel11sourceModelEv(void*);
extern int _ZNK24ColumnFilteredTableModel10columnDataEv(void*);
extern int _ZNK12ConfigFilter16primaryKeyColumnEv(void*);
extern int _ZThn8_NK12ConfigFilter16primaryKeyColumnEv(void*);
extern int _ZNK12ConfigFilter9sortOrderEv(void*);
extern int _ZThn8_NK12ConfigFilter9sortOrderEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK16InsertRowsStatus6amountEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK16InsertRowsStatus8rowIndexEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK15PasteRowsStatus3rowEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK15PasteRowsStatus6amountEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZNK17SubtreeProxyModel9rootIndexEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZThn8_NK24ColumnFilteredTableModel10columnDataEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZNK24ColumnFilteredTableModel11sourceModelEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZNK24ColumnFilteredTableModel10columnDataEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZNK12ConfigFilter16primaryKeyColumnEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,64);int r=_ZThn8_NK12ConfigFilter16primaryKeyColumnEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,71);int r=_ZNK12ConfigFilter9sortOrderEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,78);int r=_ZThn8_NK12ConfigFilter9sortOrderEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
return 0;}
