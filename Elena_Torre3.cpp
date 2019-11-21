#include <iostream>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <semaphore.h>
#include  <sys/shm.h>
#include <fcntl.h>
#include <sys/stat.h>  

struct address
{
    int pageNumb; //page number for a process
    int pageEntry; //entry on that page
};
struct instruction
{
    int pid; // process for instruction
    address ad; //address for instruction
};
struct process
{
    int pid; //process ID
    sem_t *sem;
    int pagesOnDisk; //# of pages
    std::vector<instruction> inst; //list of instructions. Since this is a single-processor simulation, each process will execute alone
    int pageFaults = 0;
};
struct frame //content of each  page frame
{
    int pid; //process address space
    int pageNumb; //page number
    bool empty;
};
struct frameTable
{
    int pid; //one frames table for each process to save context
    bool inQueue = false; //to signal that the process is on the disk queue to resolve a page fault
    bool terminated = true; //to signal the process is done executing
    address pageRequest;
    int emptyFrames;
    int maxEmpty;
    int minEmpty;
    frame frames[10]; //list of frames on Main Memory
    /*REMEMBER TO CHANGE THIS TO 1000 BEFORE SUBMITTING
    10 IS JUST FOR TESTING*/
    
};
struct masterFrameTable //struct to be placed on shared memory to give access to running processes
{
    frameTable frameTables[10];
    /*REMEMBER TO CHANGE THIS TO 100 BEFORE SUBMITTING
    10 IS JUST FOR TESTING*/
};

struct diskProcess
{
    int pid; //process ID
    int base; //where on the disk the space for this process starts
    int max; //where the space ends
};
struct diskTable
{
    int diskSize; //in bytes
    std::vector<diskProcess> dp; //vector that holds the range of addresses where each process is located
    //This will be used to check that the page table won't map two logical addresses to the same physical address
};
struct map
{
    int pageNumb; //page number
    int pAdress; //location of said page on Disk
};
struct pageList
{
    int pid; //process ID
    std::vector<map> maps; //map records the physical address of each page

};
struct pageTable
{
    std::vector<pageList> pageLists; //one pageList for each process
};

process processCreator(int id, int pages);
instruction instrCreator(int id, std::string addr);
void paging(process* processPtr, masterFrameTable* framesPtr, sem_t* dSem, sem_t* mutex);
int main()
{
    std::vector<std::string> fileCont; //string vector to store each line on the text file
    
   /*for (std::string line; std::getline(std::cin, line); )
    {
       fileCont.push_back(line); //each line is an element in this vector
    }*/
    std::ifstream inFile;

    inFile.open("inp.txt");
    if (!inFile) {
        std::cout << "Unable to open file";
        exit(1); // terminate with error
    }
    std::string line;
    while (inFile >> line) 
    {
       fileCont.push_back(line); //each line is an element in this vector
    }

    int tp = stoi(fileCont[0]); // total No. of page frames in main memory
    int ps = stoi(fileCont[1]); // page size (in bytes)
    int framesPerProcess = stoi(fileCont[2]); //frames per process or delta
    int lookahead = stoi(fileCont[3]); //for OPT and LRU-X
    int poolMin = stoi(fileCont[4]); //max  empty frames in main memory
    int poolMax = stoi(fileCont[5]); //min  empty frames in memory
    int numb_process = stoi(fileCont[6]); //number of processes

    int TotalPageFaults = 0; //total number of page faults

    std::vector<process> processList;

    fileCont.erase(fileCont.begin(), (fileCont.begin()+7));

    for(int i =0; i < numb_process; i++)
    {
        std::istringstream buff(fileCont[2*i]);
        int x;
        buff >> x;
        std::istringstream buff2(fileCont[(2*i)+ 1]);
        int y;
        buff2 >> y;
        processList.push_back(processCreator(x, y));
    }
    fileCont.erase(fileCont.begin(), (fileCont.begin()+(2*numb_process))); //erase all lines of the input file I don't need

    std::vector<instruction> instructions; //vector with pid-address pairs

    for(int i = 0; i < (fileCont.size()/2); i++)
    {
        
        std::istringstream buff(fileCont[2*i]);
        int tempPid = 0;
        buff >> tempPid;
        std::string temp = (fileCont[(2*i)+1]);
        if(temp == "-1")
        {
            instructions.push_back(instrCreator(tempPid, temp));
        }
        else
        {
            temp.erase(0, 2);
            instructions.push_back(instrCreator(tempPid, temp));
        }

    }


    /*Create Disk Table*/

    diskTable dTable; 

    dTable.diskSize = 0; //initialize at 0
    for(int i = 0; i < processList.size(); i++)
    {
        diskProcess diskP;
        dTable.diskSize += (processList[i].pagesOnDisk * ps);
        diskP.pid = processList[i].pid;
        dTable.dp.push_back(diskP);
    }
    int baseCounter = 0; //counter to set the base and max addresses for each process
    for(int i = 0; i < processList.size(); i++)
    {
        dTable.dp[i].base = baseCounter; //set base
        baseCounter += (processList[i].pagesOnDisk *ps); // use base counter to save max
        dTable.dp[i].max =baseCounter;
    }

    /*Allocating shared memory for frames table*/

    int memsize = sizeof(struct masterFrameTable);
    masterFrameTable* masterFrame;

    key_t key = 9876; //key for my shared memory
    int shmid = shmget(key, memsize, IPC_CREAT | 0666);
    if(shmid < 0)
    {
        perror("error allocating shared memory.");
    }

    masterFrame = reinterpret_cast<masterFrameTable*>(shmat(shmid, NULL, 0)); //pointer to my shared memory
    if(masterFrame == (void*)-1)
    {
        perror("error attatching shared memory.");
    }

    /*Create frame Tables for every process*/
    /*This will help me save the frames allocated to every process for context switching on a page fault*/

    
    frameTable fTable;
    fTable.maxEmpty = poolMax;
    fTable.minEmpty = poolMin;
    fTable.emptyFrames = 0;
    for(int i = 0; i < tp; i++)
    {
        frame tempFrame;

        tempFrame.pageNumb = 999;
        tempFrame.pid = 999;
        tempFrame.empty = true; //initialize empty frames
        fTable.frames[i] = tempFrame;

        fTable.emptyFrames ++;
        fTable.terminated = false;
        fTable.inQueue;

        address tempadr;
        tempadr.pageEntry = 999;
        tempadr.pageNumb = 999;
        fTable.pageRequest = tempadr;
    }
    for(int i = 0; i <processList.size(); i++)
    {
        fTable.pid = processList[i].pid;
        masterFrame->frameTables[i] = fTable;
    }


    /*Create Page Table*/

    pageTable pTable;
    
    for(int i = 0; i < processList.size(); i++)
    {
        pageList pList;
        pList.pid = processList[i].pid;
        
        for(int j = 0; j < processList[i].pagesOnDisk; j++)
        {
            map tMap;
            tMap.pageNumb = j;
            tMap.pAdress = (dTable.dp[i].base) + (j*ps);
            
            if(tMap.pAdress > dTable.dp[i].max)
            {
                perror("address out of range");
            }
            pList.maps.push_back(tMap);
        }
        pTable.pageLists.push_back(pList);
    }

    /*Assign an instruction list to each process*/
    for(int i = 0; i < processList.size(); i++)
    {
        for(int j = 0; j <instructions.size(); j++)
        {
            if(instructions[j].pid == processList[i].pid)
            {
                processList[i].inst.push_back(instructions[j]);
            }
        }
    }

    /*Create needed semaphores*/
    sem_t *mutex = sem_open("/mtx", O_CREAT, S_IRUSR | S_IWUSR, 1);
    sem_unlink("/mtx"); //unlink semaphore so that it is deleted when all the processes call sem_close

    sem_t *driver_sem = sem_open("/drive", O_CREAT, S_IRUSR | S_IWUSR, 0);
    sem_unlink("/drive");

    for(int i = 0; i <processList.size(); i++)
    {
        char j[1];
        j[0] = (char)i;
        char *name = j;
        sem_t *pSem = sem_open(name, O_CREAT, S_IRUSR | S_IWUSR, 0);
        sem_unlink(name);
        processList[i].sem = pSem;
    }
    /*Start paging*/

    pid_t pid;

    for(int i = 0; i < processList.size(); i++)
    {
        pid = fork();

        if(pid == -1)
        {
            perror("error forking.");
        }
        else if (pid == 0) // fork once for each process
        {
            /*paging function here*/

            break;
        }
    }
    if(pid != 0)
    {
        pid  = fork();

        if(pid == -1)
        {
            perror("error forking.");
        }
        if (pid == 0)
        {
            /*disk driver goes here*/
            
        }
        
        if(pid != 0)
        {
            /*page fault handler goes here*/
        }
    }

    shmdt(masterFrame); //remove frame table shared memory space
    sem_close(mutex);
    sem_close(driver_sem);
    for(int i = 0; i <processList.size(); i++)
    {
        sem_close(processList[i].sem);
    }
    return 0;


}
void paging(process* processPtr, masterFrameTable* framesPtr, sem_t* dSem, sem_t* mutex)
{
    sem_wait(processPtr->sem);
    int i = 0;
    while(i < processPtr->inst.size())
    {
        instruction currentInstr = processPtr->inst[i];
        if(currentInstr.pid = -1)
        {
            framesPtr->frameTables[i].terminated = true;
            break;
        }

    }
}
process processCreator(int id, int pages)
{
    process newProcess;
    newProcess.pid = id;
    newProcess.pagesOnDisk = pages;
    return newProcess;
}
instruction instrCreator(int id, std::string addr)
{
    instruction newInstr;
    newInstr.pid = id;
    address tempAd;
    if(addr == "-1")
    {
        tempAd.pageEntry = -1;
        tempAd.pageNumb = -1;
        newInstr.ad = tempAd;
        return newInstr;
    } 
    char pn = addr[0];
    char pe =  addr[1];

    int temp1;
    int temp2;

    switch (pn)
    {
    case 'A':
        tempAd.pageNumb = 10;
        break;
    case 'B':
        tempAd.pageNumb = 11;
        break;
    case 'C':
        tempAd.pageNumb = 12;
        break;
    case 'D':
        tempAd.pageNumb = 13;
        break;
    case 'E':
        tempAd.pageNumb = 14;
        break;
    case 'F':
        tempAd.pageNumb = 15;
        break;
    
    default:
        temp1 = (int)pn - '0';
        tempAd.pageNumb = temp1;
        break;
    }
    switch (pe)
    {
    case 'A':
        tempAd.pageEntry = 10;
        std::cout << tempAd.pageEntry;
        break;
    case 'B':
        tempAd.pageEntry= 11;
        break;
    case 'C':
        tempAd.pageEntry = 12;
        break;
    case 'D':
        tempAd.pageEntry = 13;
        break;
    case 'E':
        tempAd.pageEntry = 14;
        break;
    case 'F':
        tempAd.pageEntry = 15;
        break;
    
    default:
        temp2 = (int)pe - '0';
        tempAd.pageEntry= temp2;
        break;
    }
    std::cout << tempAd.pageEntry;
    newInstr.ad = tempAd;
    return newInstr;
}
