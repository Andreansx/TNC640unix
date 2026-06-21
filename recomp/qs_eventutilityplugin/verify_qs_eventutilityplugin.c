#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK22FilteredEventListModel5modelEv(void*);
extern int _ZNK22FilteredEventListModel8filteredEv(void*);
extern int _ZNK19EventTableViewModel22filteredEventListModelEv(void*);
extern int _ZNK19EventTableViewModel21eventDeleteAllCommandEv(void*);
extern int _ZN19EventTableViewModel9isGroupedEv(void*);
extern int _ZN19EventTableViewModel14emptyEventListEv(void*);
extern int _ZN13EventListItem7isGroupEv(void*);
extern int _ZNK28QuestionEventAnswerViewModel7commandEv(void*);
extern int _ZN28AutoSaveServiceFileViewModel26autoSaveServiceFileCommandEv(void*);
extern int _ZNK28AutoSaveServiceFileViewModel28autoSaveServiceFileListModelEv(void*);
extern int _ZN24SaveServiceFileViewModel28autoSaveServiceFileViewModelEv(void*);
extern int _ZN24SaveServiceFileViewModel22saveServiceFileCommandEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK22FilteredEventListModel5modelEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK22FilteredEventListModel8filteredEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK19EventTableViewModel22filteredEventListModelEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK19EventTableViewModel21eventDeleteAllCommandEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZN19EventTableViewModel9isGroupedEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZN19EventTableViewModel14emptyEventListEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZN13EventListItem7isGroupEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZNK28QuestionEventAnswerViewModel7commandEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZN28AutoSaveServiceFileViewModel26autoSaveServiceFileCommandEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,64);int r=_ZNK28AutoSaveServiceFileViewModel28autoSaveServiceFileListModelEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,71);int r=_ZN24SaveServiceFileViewModel28autoSaveServiceFileViewModelEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,78);int r=_ZN24SaveServiceFileViewModel22saveServiceFileCommandEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
return 0;}
