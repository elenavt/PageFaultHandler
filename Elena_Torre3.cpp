#include <iostream>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <semaphore.h>
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
    int emptyFrames;
    int maxEmpty;
    int minEmpty;
    std::vector<frame> frames; //list of frames on Main Memory
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

    /*Create frame Tables for every process*/
    /*This will help me save the frames allocated to every process for context switching on a page fault*/

    std::vector<frameTable> fTables;
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
        fTable.emptyFrames ++;
        fTable.frames.push_back(tempFrame);
    }
    for(int i = 0; i <processList.size(); i++)
    {
        fTable.pid = processList[i].pid;
        fTables.push_back(fTable);
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
                std::cout<<"ya done fucked up";
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

    /*Start paging*/

    for(int i = 0; i < processList.size(); i++)
    {
        for(int j = 0; j < processList[i].inst.size(); j++)
        {
            
        }
    }

    std::cout <<"ya";
    return 0;


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
