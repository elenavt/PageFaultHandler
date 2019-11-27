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
#include <algorithm>


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
    int pagesOnDisk; //# of pages
    std::vector<instruction> inst; //list of instructions. Since this is a single-processor simulation, each process will execute alone
};
struct frame //content of each  page frame
{
    int pageNumb; //page number
    bool empty;
    int FirstIn = 0; //for FIFO scheduling
};
struct frameTable
{
    int pid; //one frames table for each process to save context
    bool inQueue = false; //to signal that the process is on the disk queue to resolve a page fault
    bool terminated = true; //to signal the process is done executing
    int frameFreq[1000]; //for LFU  and LRU-X scheduling
    address refList[1000]; //for OPT, LRU-X and LRU
    int refIdx; //to keep trach of which instruction on the sequence we are on
    int pagesOnDisk; //pages on disk for this process
    int maxWorkingSet = 0; //for Working Set
    address pageRequest; //page being requested from disk
    int PagesReferenced = 0; //for FIFO scheduling
    int pageFaults = 0; //page faults
    int emptyFrames;
    int maxEmpty;
    int minEmpty;
    frame frames[1000]; //list of frames on Main Memory
    
};
struct masterFrameTable //struct to be placed on shared memory to give access to running processes
{
    frameTable frameTables[100];
    int totalPagefaults = 0;  //total page faults for all processes
    int totalInstructions; //to keep track of how far we are on the instruction sequence for all processes combined
    int instructionsSoFar;
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

process processCreator(int id, int pages); /*Initializes process structs*/
instruction instrCreator(int id, std::string addr); /*For extracting instructions from input file*/

/*Function called by each process to start the paging process*/
void paging(process* processPtr, masterFrameTable* framesPtr, sem_t* dSem, sem_t* mutex, sem_t* mySem, int idx, int frameCount);

/*Page Replacement Functions for each algorithm. They can all be tested within a single file by modifying the function call within Disk driver process*/
void pageReplacementRandom(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested);
void pageReplacementFIFO(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested);
void pageReplacementLDF(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested);
void pageReplacementLFU(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested);
void pageReplacementOPT(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested, int lookahead);
void pageReplacementLRU(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested);
void pageReplacementLRUX(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested, int lookahead);

/*The Working Set replacement algorithm requires 2 functions instead of one. FrameUpdate must be uncommented within the paging function
and frameSearch needs a different last paramenter value for this replacement. Further instructions within paging function code*/
void frameUpdateWS(masterFrameTable* framesPtr, int idx, int delta);
void pageReplacementWS(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested);

/*Function to check if the page being requested is already in the frames*/
bool frameSearch(frameTable ft, int pageNumb, int frameCount);
int main()
{
    std::vector<std::string> fileCont; //string vector to store each line on the text file
    
    std::ifstream inFile;

    /*Input file must be on the same file as .cpp file
    Compiled with g++ Elena_Torre3.cpp -pthread*/
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

    /*Initialze process structs*/
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
    for(int i = 0; i < numb_process; i++)
    {
        diskProcess diskP;
        dTable.diskSize += (processList[i].pagesOnDisk * ps);
        diskP.pid = processList[i].pid;
        dTable.dp.push_back(diskP);
    }
    int baseCounter = 0; //counter to set the base and max addresses for each process
    for(int i = 0; i < numb_process; i++)
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

        tempFrame.pageNumb = 9999;
        tempFrame.empty = true; //initialize empty frames
        fTable.frames[i] = tempFrame;

        fTable.emptyFrames ++;
        fTable.terminated = false;
        fTable.inQueue;

        address tempadr;
        tempadr.pageEntry = 9999;
        tempadr.pageNumb = 9999;
        fTable.pageRequest = tempadr;
    }
    for(int i = 0; i <numb_process; i++)
    {
        fTable.pid = processList[i].pid;
        fTable.pageFaults = 0;
        fTable.PagesReferenced = 0;
        fTable.refIdx = 0;
        int size = processList[i].pagesOnDisk;
        std::fill(fTable.frameFreq, fTable.frameFreq + (size + 1), 0); //making sure frameFreq table is set to all 0 for LFU and LRU-X
        fTable.pagesOnDisk = size;
        masterFrame->frameTables[i] = fTable;
    }
    masterFrame->totalInstructions = instructions.size();
    masterFrame->instructionsSoFar = 0;
    masterFrame->totalPagefaults =0;


    /*Create Page Table*/

    pageTable pTable;
    
    for(int i = 0; i < numb_process; i++)
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
   
    for(int i = 0; i < numb_process; i++)
    {
        for(int j = 0; j <instructions.size(); j++)
        {
            if(instructions[j].pid == processList[i].pid)
            {
                processList[i].inst.push_back(instructions[j]);
            }
        }
    }
    /*For OPT, LRU-X and LRU scheduling, I will have a list of the addresses we'll reference in the shared memory*/

    for(int i = 0; i <numb_process; i++)
    {
        for(int j = 0; j < processList[i].inst.size(); j++)
        {
            masterFrame->frameTables[i].refList[j] = processList[i].inst[j].ad;
        }
    }
    /*Create an execution sequence. It is the order in which the first instruction of each process appears on the instruction list*/

    int execSeq[numb_process];

    for(int i = 0; i < numb_process; i++)
    {
        execSeq[i] = 0;
    }
    int k = 0;
    for(int i =0; i <instructions.size(); i++)
    {
        for(int j = 0; j < numb_process; j++)
        {
            if(instructions[i].pid == execSeq[j])
            {
                break;
            }
            else if(execSeq[j] == 0)
            {
                execSeq[j] = instructions[i].pid;
                k++;
                break;
            }
        }
        if( k >= numb_process)
        {
            break;
        }
    }

    /*Create needed semaphores*/
    sem_t *mutex = sem_open("/mtx", O_CREAT, S_IRUSR | S_IWUSR, 1); //mutex to control access to shared memory. No 2 processes can modify it at once
    if (mutex == SEM_FAILED) {
      perror("sem_open() failed ");
    }
    sem_unlink("/mtx"); //unlink semaphore so that it is deleted when all the processes call sem_close
    
    sem_t *driver_sem = sem_open("/drive", O_CREAT, S_IRUSR | S_IWUSR, 0); //semaphore for the disk driver
    if (driver_sem == SEM_FAILED) {
      perror("sem_open() failed ");
    }
    sem_unlink("/drive");

    /*semaphore to signal to the disk driver that the processes have terminated*/
    sem_t *driver_deact = sem_open("/drive_end", O_CREAT, S_IRUSR | S_IWUSR, 0); 
    if (driver_deact == SEM_FAILED) {
      perror("sem_open() failed ");
    }
    sem_unlink("/drive_end");

    /*One named semaphore per process. Each process will wait for a signal before continuing with the next instruction*/
    sem_t *procSems[numb_process];
    for(int i = 0; i <numb_process; i++)
    {
        char j[3];
        j[0] = '/';
        j[1] = rand() % 10;
        j[2] = rand() % 10;
        char *name = j;
        procSems[i] = sem_open(name, O_CREAT, S_IRUSR | S_IWUSR, 0);
        if (procSems[i] == SEM_FAILED) {
            perror("sem_open() failed ");
        }
        sem_unlink(name);
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
            process* myPtr = &processList[i];
            paging(myPtr, masterFrame, driver_sem, mutex, procSems[i], i, framesPerProcess);
            std::cout<<"Process "<<std::to_string(i+1)<<" page faults: "<<std::to_string(masterFrame->frameTables[i].pageFaults)<<std::endl;

            /*FOR WORKING SET: uncomment line bellow to add max working set size to output*/
            //std::cout<<"Process "<<std::to_string(i+1)<<" max working set: "<<std::to_string(masterFrame->frameTables[i].maxWorkingSet)<<std::endl;
            
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
            /*Disk driver process: it will handle page replacement*/
            bool x = true;
            while(x)
            {
                int flag = sem_trywait(driver_deact); //Check if PageFault Handler process has signaled that all processes are over
                if((flag == -1)&&(errno = EAGAIN))
                {
                    int flag2 = sem_trywait(driver_sem); //check if any process has had a page fault
                    if((flag2 == -1)&&(errno = EAGAIN))
                    {

                        sleep(1); //if not wait 1 second
                    }
                    else
                    {
                        sem_wait(mutex);
                        for(int i =0; i < numb_process; i++)
                        {
                            //Find first process in queue
                            if(masterFrame->frameTables[i].inQueue == true)
                            {
                                /*Checking that address maps to the right physical address on disk*/
                                int pageWanted = masterFrame->frameTables[i].pageRequest.pageNumb;
                                int diskAddress = dTable.dp[i].base + (ps * pageWanted);
                                if(pTable.pageLists[i].maps[pageWanted].pAdress == diskAddress)
                                {
                                    /*To test any replacement algorithm except Working Set, uncomment the corresponding function call
                                    and comment all others*/
                                    //pageReplacementFIFO(masterFrame, i, framesPerProcess, masterFrame->frameTables[i].pageRequest);
                                    //pageReplacementRandom(masterFrame, i, framesPerProcess, masterFrame->frameTables[i].pageRequest);
                                    //pageReplacementLDF(masterFrame, i, framesPerProcess, masterFrame->frameTables[i].pageRequest);
                                    //pageReplacementLFU(masterFrame, i, framesPerProcess, masterFrame->frameTables[i].pageRequest);
                                    //pageReplacementLRU(masterFrame, i, framesPerProcess, masterFrame->frameTables[i].pageRequest);
                                    //pageReplacementOPT(masterFrame, i, framesPerProcess, masterFrame->frameTables[i].pageRequest, lookahead);
                                    pageReplacementLRUX(masterFrame, i, framesPerProcess, masterFrame->frameTables[i].pageRequest, lookahead);

                                    /*For Working Set uncomment function call bellow AND uncomment/modify additional function calls 
                                    WITHIN paging function*/
                                    //pageReplacementWS(masterFrame, i, masterFrame->frameTables[i].pagesOnDisk, masterFrame->frameTables[i].pageRequest);
                                    
                                    masterFrame->frameTables[i].pageFaults++;
                                    masterFrame->totalPagefaults++;
                                }
                                else
                                {
                                    perror("error on address transaltion");
                                }
                                
                                break;
                            }
                        }
                        sem_post(mutex);
                    } 

                }
                else
                {
                    x = false;
                }
                
            }   
            
        }
        
        if(pid != 0)
        {
            /*Page Fault Handler process: will give processes the signal to execute instructions depending on their position on the
            execution sequence and whether or not they are in queue/terminated*/
            int terminatedProc = 0;
            while(terminatedProc < numb_process)
            {
                terminatedProc = 0;
                int y = 0;
                for(int i = 0; i < numb_process; i++)
                {
                    for(int j = 0; j < numb_process; j++)
                    {
                        /*Select next process on execution list*/
                        if(masterFrame->frameTables[j].pid == execSeq[i])
                        {
                            break;
                        }
                        y++;
                    }
                    /* In not in queue or terminated, signal*/
                    if(masterFrame->frameTables[y].inQueue == false && masterFrame->frameTables[y].terminated == false)
                    {
                        sem_post(procSems[y]);
                        break;
                    }
                    else
                    {
                        y = 0;
                    }
                    
                }
                sem_wait(mutex);
                for(int i = 0; i < numb_process; i++) //checl how many processes have terminated
                {
                
                   if(masterFrame->frameTables[i].terminated == true)
                    {
                        terminatedProc++;
                    }
                }
                sem_post(mutex);
            }
            /*When all processes are done executing, signal disk driver to terminate as well*/
            sem_post(driver_deact);
            std::cout<<"Total page faults "<< masterFrame->totalPagefaults<<std::endl;
            
            
        }
    }

    /*Remove table from shared memory space and close all semaphores*/
    shmdt(masterFrame); 
    sem_close(mutex);
    sem_close(driver_sem);
    sem_close(driver_deact);
    for(int i = 0; i <numb_process; i++)
    {
        sem_close(procSems[i]);
    }
    return 0;


}
void pageReplacementRandom(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested)
{
    /*If there are still empty frames on table, put needed frame on one */
    if(framesPtr->frameTables[idx].emptyFrames > framesPtr->frameTables[idx].minEmpty)
    {
        for(int i =0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            if(framesPtr->frameTables[idx].frames[i].empty == true)
            {
                framesPtr->frameTables[idx].frames[i].empty = false;
                framesPtr->frameTables[idx].frames[i].pageNumb = pageRequested.pageNumb;
                framesPtr->frameTables[idx].inQueue = false;
                framesPtr->frameTables[idx].emptyFrames--;
                break;
            }
        }
    }
    /*Otherwise replace a random frame*/
    else
    {
        int random = rand() % (framesPtr->frameTables[idx].minEmpty);
        framesPtr->frameTables[idx].frames[random].empty = false;
        framesPtr->frameTables[idx].frames[random].pageNumb = pageRequested.pageNumb;
        framesPtr->frameTables[idx].inQueue = false;
    }
    
}
void pageReplacementFIFO(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested)
{
    /*If there are still empty frames on table, put needed frame on one */
    if(framesPtr->frameTables[idx].emptyFrames > framesPtr->frameTables[idx].minEmpty)
    {
        for(int i =0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            if(framesPtr->frameTables[idx].frames[i].empty == true)
            {
                framesPtr->frameTables[idx].frames[i].empty = false;
                framesPtr->frameTables[idx].frames[i].pageNumb = pageRequested.pageNumb;
                framesPtr->frameTables[idx].inQueue = false;
                framesPtr->frameTables[idx].emptyFrames--;
                framesPtr->frameTables[idx].PagesReferenced++;
                /*First In array keeps track of when each page was referenced for the first time
                We will use these values bellow to know which frame to replace*/
                framesPtr->frameTables[idx].frames[i].FirstIn = framesPtr->frameTables[idx].PagesReferenced;
                break;
            }
        }
    }
    else
    {
        int FIFO = INT16_MAX;
        int winnerIdx = 0;
        for(int i = 0; i <(framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            /*We replace the frame whose First in value is smallest*/
            if(framesPtr->frameTables[idx].frames[i].FirstIn < FIFO)
            {
                FIFO = framesPtr->frameTables[idx].frames[i].FirstIn;
                winnerIdx = i;

            }
        }
        framesPtr->frameTables[idx].frames[winnerIdx].empty = false;
        framesPtr->frameTables[idx].frames[winnerIdx].pageNumb = pageRequested.pageNumb;
        framesPtr->frameTables[idx].inQueue = false;
        framesPtr->frameTables[idx].PagesReferenced++;
        framesPtr->frameTables[idx].frames[winnerIdx].FirstIn = framesPtr->frameTables[idx].PagesReferenced;

    }
    
}
void pageReplacementLDF(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested)
{
    /*If there are still empty frames on table, put needed frame on one */
     if(framesPtr->frameTables[idx].emptyFrames > framesPtr->frameTables[idx].minEmpty)
    {
        for(int i =0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            if(framesPtr->frameTables[idx].frames[i].empty == true)
            {
                framesPtr->frameTables[idx].frames[i].empty = false;
                framesPtr->frameTables[idx].frames[i].pageNumb = pageRequested.pageNumb;
                framesPtr->frameTables[idx].inQueue = false;
                framesPtr->frameTables[idx].emptyFrames--;
                break;
            }
        }
    }
    else
    {
        int maxDistance = 0;
        int winnerIdx;
        for(int i =0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            /*We replace the frame whose page is at the largest distance from the page requested*/
            int distance = abs(framesPtr->frameTables[idx].frames[i].pageNumb - pageRequested.pageNumb);
            if(distance > maxDistance)
            {
                maxDistance = distance;
                winnerIdx = i;
            }
        }
        framesPtr->frameTables[idx].frames[winnerIdx].empty = false;
        framesPtr->frameTables[idx].frames[winnerIdx].pageNumb = pageRequested.pageNumb;
        framesPtr->frameTables[idx].inQueue = false;
    }
    
}
void pageReplacementLFU(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested)
{
    /*If there are still empty frames on table, put needed frame on one */
     if(framesPtr->frameTables[idx].emptyFrames > framesPtr->frameTables[idx].minEmpty)
    {
        for(int i =0; i <(framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            if(framesPtr->frameTables[idx].frames[i].empty == true)
            {
                framesPtr->frameTables[idx].frames[i].empty = false;
                framesPtr->frameTables[idx].frames[i].pageNumb = pageRequested.pageNumb;
                framesPtr->frameTables[idx].inQueue = false;
                framesPtr->frameTables[idx].emptyFrames--;
                /*Frame frequency array will be used to keep track of how many times a page has been referenced*/
                framesPtr->frameTables[idx].frameFreq[pageRequested.pageNumb]++;
                break;
            }
        }
    }
    else
    {
        int minFreq = INT16_MAX;
        int winnerIdx = 0;
        for(int i =0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            /*Select frame with smallest frequency*/
            int freq = framesPtr->frameTables[idx].frameFreq[framesPtr->frameTables[idx].frames[i].pageNumb];
            if(freq < minFreq)
            {
                minFreq = freq;
                winnerIdx = i;
            }
        }
        framesPtr->frameTables[idx].frames[winnerIdx].empty = false;
        framesPtr->frameTables[idx].frames[winnerIdx].pageNumb = pageRequested.pageNumb;
        framesPtr->frameTables[idx].inQueue = false;
        framesPtr->frameTables[idx].frameFreq[pageRequested.pageNumb]++;

    }
    
}
void pageReplacementOPT(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested, int lookahead)
{
    /*If there are still empty frames on table, put needed frame on one */
    if(framesPtr->frameTables[idx].emptyFrames > framesPtr->frameTables[idx].minEmpty)
    {
        for(int i =0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            if(framesPtr->frameTables[idx].frames[i].empty == true)
            {
                framesPtr->frameTables[idx].frames[i].empty = false;
                framesPtr->frameTables[idx].frames[i].pageNumb = pageRequested.pageNumb;
                framesPtr->frameTables[idx].inQueue = false;
                framesPtr->frameTables[idx].emptyFrames--;
                framesPtr->frameTables[idx].refIdx++;
                break;
            }
        }
    }
    else
    {
        int maxRefDistance = -1;
        int winnerIdx =0;
        for(int i =0; i <(framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            /*We iterate through the instruction sequence for the process to find the frame whose next reference is furthest away*/
            int refDis = 0;
            for(int j = framesPtr->frameTables[idx].refIdx; j <= (framesPtr->frameTables[idx].refIdx + lookahead); j++)
            {
                if(framesPtr->frameTables[idx].frames[i].pageNumb != framesPtr->frameTables[idx].refList[j].pageNumb)
                {
                    refDis++;
                }
                else
                {
                    break;
                }
                
            }
            if(refDis > maxRefDistance)
            {
                maxRefDistance = refDis;
                winnerIdx = i;
            }
        }
        framesPtr->frameTables[idx].frames[winnerIdx].empty = false;
        framesPtr->frameTables[idx].frames[winnerIdx].pageNumb = pageRequested.pageNumb;
        framesPtr->frameTables[idx].inQueue = false;
        framesPtr->frameTables[idx].refIdx++;

    }
}
void pageReplacementWS(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested)
{
    /*This function will be used to add the needed page to the working set in case of a page fault*/
    for(int i =0; i < framesPtr->frameTables[idx].pagesOnDisk; i++)
        {
            if(framesPtr->frameTables[idx].frames[i].empty == true)
            {
                framesPtr->frameTables[idx].frames[(i)].empty = false;
                framesPtr->frameTables[idx].frames[(i)].pageNumb = pageRequested.pageNumb;
                framesPtr->frameTables[idx].inQueue = false;
                break;
            }
        }
}
void frameUpdateWS(masterFrameTable* framesPtr, int idx, int delta)
{
    /*This function will be called within the paging function to make sure the working set stays current*/
    framesPtr->frameTables[idx].refIdx++; //move reference idx and put the frames of the last 4 references on frame table
    int j;
    if((framesPtr->frameTables[idx].refIdx - delta) <= 0)
    {
        j =0;
    }
    else
    {
        j = framesPtr->frameTables[idx].refIdx - delta;
        
    }
    
    /*Delete preexisting frames so we can fill frame table with only the last delta references*/
    for(int i = 0; i < framesPtr->frameTables[idx].pagesOnDisk; i++)
    {
        if(framesPtr->frameTables[idx].frames[i].empty == false)
        {
            framesPtr->frameTables[idx].frames[i].pageNumb = 99999;
            framesPtr->frameTables[idx].frames[i].empty = true;
        }
    }
    bool flag2 = true;
    int i = 0;
    /*Add the pages that have been referenced on the last delta instructions*/
    while(flag2)
    {
        if(j < framesPtr->frameTables[idx].refIdx)
        {
            int counter = 0;
            for(int h = 0; h < framesPtr->frameTables[idx].pagesOnDisk; h++)
            {
                if(framesPtr->frameTables[idx].frames[h].pageNumb == framesPtr->frameTables[idx].refList[j].pageNumb)
                {
                    break;
                }
                else
                {
                    counter++;
                }
                
            }
            if(counter >= framesPtr->frameTables[idx].pagesOnDisk)
            {
                framesPtr->frameTables[idx].frames[i].empty = false;
                framesPtr->frameTables[idx].frames[i].pageNumb = framesPtr->frameTables[idx].refList[j].pageNumb;
            }
            i++;
            j++;

        }
        else
        {
            flag2 = false;
        }
        
    }
    bool flag = true;
    int maxWS = 0;
    i = 0;
    /*Check the current max working set size*/
    while(flag)
    {
        if(framesPtr->frameTables[idx].frames[i].empty == true)
        {
            if(framesPtr->frameTables[idx].maxWorkingSet < maxWS)
            {
                framesPtr->frameTables[idx].maxWorkingSet = maxWS;
            }
            flag = false;
        }
        else
        {
            maxWS++;
            i++;
        }
        
    }
}
void pageReplacementLRU(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested)
{
    /*If there are still empty frames on table, put needed frame on one */
    if(framesPtr->frameTables[idx].emptyFrames > framesPtr->frameTables[idx].minEmpty)
    {
        for(int i =0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            if(framesPtr->frameTables[idx].frames[i].empty == true)
            {
                framesPtr->frameTables[idx].frames[i].empty = false;
                framesPtr->frameTables[idx].frames[i].pageNumb = pageRequested.pageNumb;
                framesPtr->frameTables[idx].inQueue = false;
                framesPtr->frameTables[idx].emptyFrames--;
                framesPtr->frameTables[idx].refIdx++;
                break;
            }
        }
    }
    else
    {
        int maxDistance = 0;
        int winnerIdx;
        /*Replace the page whose last reference is furthest away*/
        for(int i = 0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            int distanceToLastUse = 0;
            for(int j = framesPtr->frameTables[idx].refIdx; j >= 0; j--)
            {
                if(framesPtr->frameTables[idx].frames[i].pageNumb != framesPtr->frameTables[idx].refList[j].pageNumb)
                {
                    distanceToLastUse++;
                }
                else
                {
                    break;
                }
                
            }
            if(distanceToLastUse > maxDistance)
            {
                maxDistance = distanceToLastUse;
                winnerIdx =i;
            }
        }
        framesPtr->frameTables[idx].frames[winnerIdx].empty = false;
        framesPtr->frameTables[idx].frames[winnerIdx].pageNumb = pageRequested.pageNumb;
        framesPtr->frameTables[idx].inQueue = false;
        framesPtr->frameTables[idx].refIdx++;
    }
}
void pageReplacementLRUX(masterFrameTable* framesPtr, int idx, int frameCount, address pageRequested, int lookahead)
{
    /*If there are still empty frames on table, put needed frame on one */
    if(framesPtr->frameTables[idx].emptyFrames > framesPtr->frameTables[idx].minEmpty)
    {
        for(int i =0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            if(framesPtr->frameTables[idx].frames[i].empty == true)
            {
                framesPtr->frameTables[idx].frames[i].empty = false;
                framesPtr->frameTables[idx].frames[i].pageNumb = pageRequested.pageNumb;
                framesPtr->frameTables[idx].inQueue = false;
                framesPtr->frameTables[idx].emptyFrames--;
                framesPtr->frameTables[idx].frameFreq[pageRequested.pageNumb]++;
                framesPtr->frameTables[idx].refIdx++;
                break;
            }
        }
    }
    else
    {
        std::vector<int> haveBeenReferencedXtimes; //to save the indexes of the frames that have been referenced more than x times
        int counter = 0;
        for(int i = 0; i < (framesPtr->frameTables[idx].maxEmpty - framesPtr->frameTables[idx].minEmpty); i++)
        {
            int ref = framesPtr->frameTables[idx].frameFreq[framesPtr->frameTables[idx].frames[i].pageNumb];
            if(ref >= lookahead)
            {
                haveBeenReferencedXtimes.push_back(i);
                counter++;
            }
        }
        if(counter == 0)
        {
            /*If no page in frames has been referenced X times or more just use normal LRU*/
            pageReplacementLRU(framesPtr, idx, frameCount, pageRequested);
        }
        else 
        {
            /*Else find the page whose Xth reference is furthest away*/
            int maxDistanceToXRef = 0;
            int winnerIdx;
            for(int i = 0; i < haveBeenReferencedXtimes.size(); i++)
            {
                int distanceToXRef = 0;
                int refCount = 0;
                int j = framesPtr->frameTables[idx].refIdx;
                while(refCount < lookahead || j >= 0)
                {
                        if(framesPtr->frameTables[idx].frames[haveBeenReferencedXtimes[i]].pageNumb != framesPtr->frameTables[idx].refList[j].pageNumb)
                        {
                            distanceToXRef++;
                            j--;

                        }
                        else
                        {
                            distanceToXRef++;
                            refCount++;
                            j--;
                        }

                }
                if(distanceToXRef > maxDistanceToXRef)
                {
                    winnerIdx = haveBeenReferencedXtimes[i];
                    maxDistanceToXRef = distanceToXRef;
                }

            }
            framesPtr->frameTables[idx].frames[winnerIdx].empty = false;
            framesPtr->frameTables[idx].frames[winnerIdx].pageNumb = pageRequested.pageNumb;
            framesPtr->frameTables[idx].inQueue = false;
            framesPtr->frameTables[idx].refIdx++;

        }
        
    }
    
}
bool frameSearch(frameTable* ft, int pageNumb, int frameCount)
{
    /*Function to search for a frame in frame table*/    
    int flag = 0;
    for(int i = 0; i < frameCount; i++)
    {
        if(ft->frames[i].pageNumb == pageNumb)
        {
            return true;
        }
        flag++;
    }
    if(flag >= frameCount)
    {
        return false;
    }
    else
    {
        return false;
    }
    
}
void paging(process* processPtr, masterFrameTable* framesPtr, sem_t* dSem, sem_t* mutex, sem_t* mySem, int idx, int frameCount)
{
    
    int i = 0;
    
    while(i < processPtr->inst.size())
    {
        sem_wait(mySem); //wait for Page Fault handler to signal
        instruction currentInstr = processPtr->inst[i];
        if(currentInstr.ad.pageEntry == -1) //if page number is -1 then process has terminated
        {
            sem_wait(mutex);
            framesPtr->frameTables[idx].terminated = true;
            sem_post(mutex);
            break;
        }
        else
        {
            /*IF USING WORKIGNG SET :
                REPLACE frameCount WITH framesPtr->frameTables[idx].pagesOnDisk as the last parameter on frameSearch*/
            /*FOR ALL OTHER REPLACEMENT ALGORITHMS USE frameCount */
            bool inFrames = frameSearch(&framesPtr->frameTables[idx], currentInstr.ad.pageNumb, frameCount);
            if(inFrames == true)
            {
                sem_wait(mutex);
                framesPtr->instructionsSoFar++;
                /*EXCLUSIVE TO WORKING SET: Uncomment function call bellow. Instead of page replacement we will "update" the working set*/
                //frameUpdateWS(framesPtr, idx, frameCount); 
                sem_post(mutex);
                i++;
            }
            else
            {
                /*If page is not in frames, there is a page fault*/
                sem_wait(mutex);
                framesPtr->frameTables[idx].pageRequest = currentInstr.ad; //put page request on shared memory
                framesPtr->frameTables[idx].inQueue = true; //set inQueue to true so the page fault handler doesn't signal
                sem_post(dSem); //signal the disk driver process to add or replace the requested page
                sem_post(mutex);
            }
        
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

    newInstr.ad = tempAd;
    return newInstr;
}
