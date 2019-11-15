#include <iostream>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <semaphore.h>
struct process
{
    int pid;
    int pagesOnDisk;
};
struct instruction
{
    int pid;
    std::string address;
};
process processCreator(int id, int pages);
instruction instrCreator(int id, std::string address);

int main()
{
    std::vector<std::string> fileCont; //string vector to store each line on the text file
    
   for (std::string line; std::getline(std::cin, line); )
    {
       fileCont.push_back(line); //each line is an element in this vector
    }

    int tp = stoi(fileCont[0]); // total No. of page frames in main memory
    int ps = stoi(fileCont[1]); // page size (in bytes)
    int framesPerProcess = stoi(fileCont[2]); //frames per process or delta
    int lookahead = stoi(fileCont[3]); //for OPT and LRU-X
    int poolMin = stoi(fileCont[4]); //max frames in main memory
    int poolMax = stoi(fileCont[5]); //min frames in memory
    int numb_process = stoi(fileCont[6]); //number of processes
    std::vector<process> processList;
    for(int i = 0; i <numb_process; i++) //create vector of process structs 
    {
        std::istringstream buff(fileCont[7+i]);
        int tempPid = 0;
        buff >> tempPid;
        int tempPages = 0;
        buff >> tempPages;
        processList.push_back(processCreator(tempPid, tempPages));

    }
    fileCont.erase(fileCont.begin(), (fileCont.begin()+7+numb_process)); //erase all lines of the input file I don't need
    std::vector<instruction> instructions; //vector with pid-address pairs
    for(int i = 0; i < fileCont.size(); i++)
    {
        
        std::istringstream buff(fileCont[i]);
        int tempPid = 0;
        buff >> tempPid;
        std::string temp;
        buff >> temp;
        instructions.push_back(instrCreator(tempPid, temp));
        
    }

    return 0;


}
process processCreator(int id, int pages)
{
    process newProcess;
    newProcess.pid = id;
    newProcess.pagesOnDisk = pages;
    return newProcess;
}
instruction instrCreator(int id, std::string address)
{
    instruction newInstr;
    newInstr.pid = id;
    newInstr.address = address;
    return newInstr;
}
