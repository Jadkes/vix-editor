#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <chrono>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <cctype>

// ANSI Colors
#define MAGENTA "\033[1;35m"
#define CYAN    "\033[1;36m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define RED     "\033[1;31m"
#define BLUE    "\033[1;34m"
#define WHITE   "\033[1;37m"
#define GREY    "\033[1;90m"
#define RESET   "\033[0m"


// --- Helper Tools ---

// Simple tokenizer
std::vector<std::string> split_string(const std::string& str, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        if(!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

// --- The Agent Class ---

class JarvisAgent
{
private:
    // Named constants
    static constexpr int LS_COLUMNS = 5;

    PyObject *pModule;
    PyObject *pFuncProcess;
    PyObject *pFuncGenerate;
    std::string lastGeneratedCode;

    void initPython()
    {
        std::cout << GREY << "[System] Initializing Neural Interface (Python)..." << RESET << std::endl;
        Py_Initialize();

        // Get path to executable
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        std::string exePath = (count != -1) ? std::string(result, count) : "";
        std::string exeDir = exePath.substr(0, exePath.find_last_of("/"));

        PyRun_SimpleString("import sys");
        PyRun_SimpleString("import os");
        std::string addPathCmd = "sys.path.append('" + exeDir + "')";
        PyRun_SimpleString(addPathCmd.c_str());

        PyObject *pName = PyUnicode_DecodeFSDefault("vix_brain");
        pModule = PyImport_Import(pName);
        Py_DECREF(pName);

        if (pModule == nullptr) {
            PyErr_Print();
            std::cerr << RED << "[CRITICAL] Brain module missing!" << RESET << std::endl;
            exit(1);
        }

        pFuncProcess = PyObject_GetAttrString(pModule, "process_input");
        pFuncGenerate = PyObject_GetAttrString(pModule, "generate_cpp");

        if (!pFuncProcess || !pFuncGenerate) {
            std::cerr << RED << "[CRITICAL] Brain functions missing!" << RESET << std::endl;
            exit(1);
        }
        std::cout << GREY << "[System] Interface Linked." << RESET << std::endl;
    }

    std::string callPython(PyObject* func, std::string arg)
    {
        if (!func) return "Error: Function not loaded";

        PyObject *pArgs = PyTuple_New(1);
        PyObject *pValue = PyUnicode_FromString(arg.c_str());
        PyTuple_SetItem(pArgs, 0, pValue);

        PyObject *pResult = PyObject_CallObject(func, pArgs);
        Py_DECREF(pArgs);

        std::string result = "";
        if (pResult != nullptr) {
            result = PyUnicode_AsUTF8(pResult);
            Py_DECREF(pResult);
        } else {
            PyErr_Print();
            result = "[Python Error]";
        }
        return result;
    }

public:
    JarvisAgent()
    {
        initPython();
        lastGeneratedCode = "";
    }

    ~JarvisAgent()
    {
        Py_XDECREF(pFuncProcess);
        Py_XDECREF(pFuncGenerate);
        Py_XDECREF(pModule);
        Py_Finalize();
    }

    void cmd_time()
    {
        time_t now = time(0);
        char* dt = ctime(&now);
        std::string time_str(dt);
        time_str.pop_back();
        std::cout << CYAN << ">> " << RESET << time_str << std::endl;
    }

    void cmd_ls()
    {
        DIR *dir;
        struct dirent *ent;
        std::cout << CYAN << ">> Listing current directory:" << RESET << std::endl;
        if ((dir = opendir (".")) != NULL) {
            int count = 0;
            while ((ent = readdir (dir)) != NULL) {
                std::string name = ent->d_name;
                if(name == "." || name == "..") continue;
                if (ent->d_type == DT_DIR) std::cout << BLUE << name << "/" << RESET << "  ";
                else std::cout << WHITE << name << RESET << "  ";

                count++;
                if(count % LS_COLUMNS == 0) std::cout << std::endl;
            }
            std::cout << std::endl;
            closedir (dir);
        } else {
            perror ("");
        }
    }

    void cmd_stats()
    {
        std::cout << CYAN << ">> Agent Status Report:" << RESET << std::endl;
        // Read memory usage from /proc/self/status
        std::ifstream status_file("/proc/self/status");
        std::string line;
        while(std::getline(status_file, line)) {
            if(line.find("VmRSS") != std::string::npos || line.find("VmSize") != std::string::npos) {
                std::cout << "   " << line << std::endl;
            }
        }
        status_file.close();
        std::cout << "   Core: Online (C++ Native)" << std::endl;
        std::cout << "   Brain: Connected (Python 3.12 Embed)" << std::endl;
    }

    void cmd_code(std::string topic)
    {
        std::cout << YELLOW << "[Generating C++ Code for '" << topic << "']..." << RESET << std::endl;
        std::string code = callPython(pFuncGenerate, topic);
        lastGeneratedCode = code;

        std::cout << "------------------------------------------" << std::endl;
        std::cout << GREEN << code << RESET << std::endl;
        std::cout << "------------------------------------------" << std::endl;
        std::cout << GREY << "(Type 'save <filename>' to save this snippet)" << RESET << std::endl;
    }

    void cmd_save(std::string filename)
    {
        if(lastGeneratedCode.empty()) {
            std::cout << RED << ">> No code to save! Generate something first." << RESET << std::endl;
            return;
        }

        std::ofstream out(filename);
        if(out.is_open()) {
            out << lastGeneratedCode;
            out.close();
            std::cout << GREEN << ">> Code saved to " << filename << RESET << std::endl;
        } else {
            std::cout << RED << ">> Error writing to file." << RESET << std::endl;
        }
    }

    void chat(std::string input)
    {
        std::string response = callPython(pFuncProcess, input);
        std::cout << MAGENTA << "Jarvis: " << RESET << response << std::endl;
    }

    void run()
    {
        std::cout << MAGENTA << "\n╔════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  JARVIS AGENT: CODE & ASSIST               ║" << std::endl;
        std::cout << "║  C++ Core | Python Intelligence            ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════╝" << RESET << std::endl;
        std::cout << GREY << "Type 'help' for agent commands." << RESET << std::endl;

        std::string input;
        while(true) {
            std::cout << CYAN << "\nYou: " << RESET;
            std::getline(std::cin, input);
            if(input.empty()) continue;

            std::vector<std::string> tokens = split_string(input, ' ');
            std::string cmd = tokens[0];
            for(auto& c : cmd) c = std::tolower(c);

            if(cmd == "exit" || cmd == "quit") {
                break;
            } else if(cmd == "help") {
                std::cout << YELLOW << "Agent Commands:" << RESET << std::endl;
                std::cout << "  code <topic>   : Generate C++ code (e.g., 'code class', 'code loop')" << std::endl;
                std::cout << "  save <file>    : Save the generated code to a file" << std::endl;
                std::cout << "  ls             : List files in current directory" << std::endl;
                std::cout << "  stats          : Show agent memory usage" << std::endl;
                std::cout << "  time           : Show current time" << std::endl;
                std::cout << "  clear          : Clear screen" << std::endl;
                std::cout << "  [text]         : Chat with the vix_brain" << std::endl;
            } else if(cmd == "time") cmd_time();
            else if(cmd == "ls" || cmd == "list") cmd_ls();
            else if(cmd == "stats") cmd_stats();
            else if(cmd == "clear") system("clear");
            else if(cmd == "code") {
                if(tokens.size() < 2) std::cout << RED << "Usage: code <topic>" << RESET << std::endl;
                else cmd_code(tokens[1]);
            } else if(cmd == "save") {
                if(tokens.size() < 2) std::cout << RED << "Usage: save <filename>" << RESET << std::endl;
                else cmd_save(tokens[1]);
            } else {
                // Pass full string to chat
                chat(input);
            }
        }
        std::cout << MAGENTA << "Jarvis: Systems Disengaging." << RESET << std::endl;
    }
};

int main()
{
    JarvisAgent agent;
    agent.run();
    return 0;
}