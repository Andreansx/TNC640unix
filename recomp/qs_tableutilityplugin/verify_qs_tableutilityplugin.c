#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK10JhCsvTable19autodetectDelimiterEv(void*);
extern int _ZNK10JhCsvTable12outputFormatEv(void*);
extern int _ZN11TableColumn14getColumnIndexEv(void*);
extern int _ZNK11TableColumn12choicesModelEv(void*);
extern int _ZNK25TableLayoutRepresentation7isReadyEv(void*);
extern int _ZNK25TableLayoutRepresentation7isDirtyEv(void*);
extern int _ZNK10TableModel10columnDataEv(void*);
extern int _ZThn8_NK10TableModel10columnDataEv(void*);
extern int _ZNK10TableModel10dataSourceEv(void*);
extern int _ZNK24StringTableDataInterface10columnDataEv(void*);
extern int _ZN17InstantiatedTable9recordSetEv(void*);
extern int _ZNK17InstantiatedTable13hasReadConfigEv(void*);
extern int _ZNK8CsvTable7isValidEv(void*);
extern int _ZNK8CsvTable8rowCountEv(void*);
extern int _ZNK8CsvTable11columnCountEv(void*);
extern int _ZNK8CsvTable17applySanitizationEv(void*);
extern int _ZNK8jhtables30AbstractEditingBehaviorPrivate7canEditEv(void*);
extern int _ZNK8jhtables30AbstractEditingBehaviorPrivate7canUndoEv(void*);
extern int _ZNK8jhtables30AbstractEditingBehaviorPrivate7canRedoEv(void*);
extern int _ZNK8jhtables30AbstractEditingBehaviorPrivate9canCancelEv(void*);
extern int _ZNK8jhtables30AbstractEditingBehaviorPrivate15isInTransactionEv(void*);
extern int _ZNK8jhtables30AbstractEditingBehaviorPrivate9isCachingEv(void*);
extern int _ZNK8jhtables23AbstractEditingBehavior10isReadOnlyEv(void*);
extern int _ZNK8jhtables26PocketTableEditingBehavior19toolLoadingBehaviorEv(void*);
extern int _ZNK8jhtables18AbstractTableModel5readyEv(void*);
extern int _ZNK8jhtables18AbstractTableModel14isSortingDirtyEv(void*);
extern int _ZN8jhtables10TableModel13updateHandlerEv(void*);
extern int _ZNK8jhtables20ModifiableTableModel15editingBehaviorEv(void*);
extern int _ZNK8jhtables20ModifiableTableModel16checkingBehaviorEv(void*);
extern int _ZNK8jhtables24AbstractCheckingBehavior10tableModelEv(void*);
extern int _ZNK8jhtables19BaseConfigFieldRule10tableModelEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK10JhCsvTable19autodetectDelimiterEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK10JhCsvTable12outputFormatEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZN11TableColumn14getColumnIndexEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK11TableColumn12choicesModelEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,29);fill(d,1024,55);*(void**)(o+8)=d;int r=_ZNK25TableLayoutRepresentation7isReadyEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,36);fill(d,1024,68);*(void**)(o+8)=d;int r=_ZNK25TableLayoutRepresentation7isDirtyEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZNK10TableModel10columnDataEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZThn8_NK10TableModel10columnDataEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZNK10TableModel10dataSourceEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,64);int r=_ZNK24StringTableDataInterface10columnDataEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,71);fill(d,1024,133);*(void**)(o+8)=d;int r=_ZN17InstantiatedTable9recordSetEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,78);fill(d,1024,146);*(void**)(o+8)=d;int r=_ZNK17InstantiatedTable13hasReadConfigEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,85);int r=_ZNK8CsvTable7isValidEv(o);int n=sprintf(buf,"%d %d\n",12,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,92);fill(d,1024,172);*(void**)(o+8)=d;int r=_ZNK8CsvTable8rowCountEv(o);int n=sprintf(buf,"%d %d\n",13,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,99);fill(d,1024,185);*(void**)(o+4)=d;int r=_ZNK8CsvTable11columnCountEv(o);int n=sprintf(buf,"%d %d\n",14,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,106);int r=_ZNK8CsvTable17applySanitizationEv(o);int n=sprintf(buf,"%d %d\n",15,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,113);int r=_ZNK8jhtables30AbstractEditingBehaviorPrivate7canEditEv(o);int n=sprintf(buf,"%d %d\n",16,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,120);int r=_ZNK8jhtables30AbstractEditingBehaviorPrivate7canUndoEv(o);int n=sprintf(buf,"%d %d\n",17,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,127);int r=_ZNK8jhtables30AbstractEditingBehaviorPrivate7canRedoEv(o);int n=sprintf(buf,"%d %d\n",18,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,134);int r=_ZNK8jhtables30AbstractEditingBehaviorPrivate9canCancelEv(o);int n=sprintf(buf,"%d %d\n",19,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,141);int r=_ZNK8jhtables30AbstractEditingBehaviorPrivate15isInTransactionEv(o);int n=sprintf(buf,"%d %d\n",20,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,148);int r=_ZNK8jhtables30AbstractEditingBehaviorPrivate9isCachingEv(o);int n=sprintf(buf,"%d %d\n",21,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,155);fill(d,1024,289);*(void**)(o+8)=d;int r=_ZNK8jhtables23AbstractEditingBehavior10isReadOnlyEv(o);int n=sprintf(buf,"%d %d\n",22,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,162);fill(d,1024,302);*(void**)(o+8)=d;int r=_ZNK8jhtables26PocketTableEditingBehavior19toolLoadingBehaviorEv(o);int n=sprintf(buf,"%d %d\n",23,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,169);fill(d,1024,315);*(void**)(o+8)=d;int r=_ZNK8jhtables18AbstractTableModel5readyEv(o);int n=sprintf(buf,"%d %d\n",24,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,176);fill(d,1024,328);*(void**)(o+8)=d;int r=_ZNK8jhtables18AbstractTableModel14isSortingDirtyEv(o);int n=sprintf(buf,"%d %d\n",25,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,183);fill(d,1024,341);*(void**)(o+8)=d;int r=_ZN8jhtables10TableModel13updateHandlerEv(o);int n=sprintf(buf,"%d %d\n",26,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,190);int r=_ZNK8jhtables20ModifiableTableModel15editingBehaviorEv(o);int n=sprintf(buf,"%d %d\n",27,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,197);int r=_ZNK8jhtables20ModifiableTableModel16checkingBehaviorEv(o);int n=sprintf(buf,"%d %d\n",28,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,204);int r=_ZNK8jhtables24AbstractCheckingBehavior10tableModelEv(o);int n=sprintf(buf,"%d %d\n",29,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,211);int r=_ZNK8jhtables19BaseConfigFieldRule10tableModelEv(o);int n=sprintf(buf,"%d %d\n",30,r);write(1,buf,n);}
return 0;}
