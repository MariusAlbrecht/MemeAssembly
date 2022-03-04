/*
This file is part of the MemeAssembly compiler.

 Copyright © 2021 Tobias Kamm and contributors

MemeAssembly is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

MemeAssembly is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with MemeAssembly. If not, see <https://www.gnu.org/licenses/>.
*/

#include "translator.h"
#include "../logger/log.h"

#include <time.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

///STABS flags
#define N_SO 100
#define N_SLINE 68
#define N_FUN 36
#define N_LBRAC 0xc0
#define N_RBRAC 0xe0

extern const char* const version_string;
extern const struct command commandList[];

struct translatorState {
    //Required for stabs so that the function name can be inserted into the return label
    char *currentFunctionName;
    //If the previous command was ignored and the next label was already printed, this variable is set to 1 so that the label isn't printed twice
    uint8_t DNLABEL;
};

/**
 * Creates the first STABS entry in which the origin file is stored
 * @param outputFile the output file
 */
void stabs_writeFileInfo(FILE *outputFile, char* inputFileString) {
    //Check if the input file string starts with a /. If it does, it is an absolute path
    char cwd[PATH_MAX + 1];
    if(inputFileString[0] == '/') {
        fprintf(outputFile, ".stabs \"%s\", %d, 0, 0, .Ltext0\n", inputFileString, N_SO);
    } else {
        fprintf(outputFile, ".stabs \"%s/%s\", %d, 0, 0, .Ltext0\n", getcwd(cwd, PATH_MAX), inputFileString, N_SO);
    }
}

/**
 * Checks if the current command should not receive a separate STABS line info. Currently, this only affects breakpoints
 * @param opcode the opcode
 * @return 1 if it should be ignored, 0 if it needs line info
 */
int stabs_ignore(int opcode) {
    if(strcmp(commandList[opcode].translationPattern, "int3") == 0) {
        return 1;
    }
    return 0;
}

/**
 * Creates a function info STABS of a given function
 * @param outputFile the output file
 * @param functionName the name of the function
 */
void stabs_writeFunctionInfo(FILE *outputFile, char* functionName) {
    fprintf(outputFile, ".stabs \"%s:F1\", %d, 0, 0, %s\n", functionName, N_FUN, functionName);
    fprintf(outputFile, ".stabn %d, 0, 0, %s\n", N_LBRAC, functionName);
    fprintf(outputFile, ".stabn %d, 0, 0, .Lret_%s\n", N_RBRAC, functionName);
}

/**
 * Is called after a function return command is found. Creates a label for the function info stab to use
 * @param outputFile the output file
 */
void stabs_writeFunctionEndLabel(FILE *outputFile, struct translatorState* translatorState) {
    fprintf(outputFile, "\t.Lret_%s:\n", translatorState -> currentFunctionName);
}

/**
 * Creates a label for the line number STABS to use
 * @param outputFile the output file
 * @param parsedCommand the command that requires a line number info
 */
void stabs_writeLineLabel(FILE *outputFile, struct parsedCommand parsedCommand) {
    fprintf(outputFile, "\t.Lcmd_%d:\n", parsedCommand.lineNum);
}

/**
 * Creates a line number STABS of the provided command
 * @param outputFile the output file
 * @param parsedCommand the command that requires a line number info
 */
void stabs_writeLineInfo(FILE *outputFile, struct parsedCommand parsedCommand) {
    fprintf(outputFile, "\t.stabn %d, 0, %d, .Lcmd_%d\n", N_SLINE, parsedCommand.lineNum, parsedCommand.lineNum);
}

/**
 * Translates a given command into assembly. This includes inserting parameters and creating STABS info if necessary
 * @param commandsArray all commands that were parsed
 * @param index the index of the current command
 * @param outputFile the output file
 */
void translateToAssembly(struct compileState* compileState, struct translatorState* translatorState, size_t index, FILE *outputFile) {
    struct parsedCommand parsedCommand = compileState -> commandsArray.arrayPointer[index];
    if(parsedCommand.opcode != 0 && compileState->optimisationLevel == o42069) {
        printDebugMessage("\tCommand is not a function declaration, abort.", "", compileState -> logLevel);
        return;
    }

    //If we are supposed to create STABS info, we now need to create labels
    if(compileState -> useStabs) {
        //If this is a function declaration, update the current function name
        if(parsedCommand.opcode == 0) {
            translatorState -> currentFunctionName = parsedCommand.parameters[0];
        //If this command is supposed to be ignored
        } else if (stabs_ignore(parsedCommand.opcode)) {
            //Already print the start label of the next command
            stabs_writeLineLabel(outputFile, compileState -> commandsArray.arrayPointer[index + 1]);
            //Set the DNLABEL variable
            translatorState -> DNLABEL = 1;
        //If it is a regular command
        } else if (translatorState -> DNLABEL == 0) {
            stabs_writeLineLabel(outputFile, parsedCommand);
        //If the previous command was ignored, i.e. DNLABEL was set, reset it to 0
        } else {
            translatorState -> DNLABEL = 0;
        }
    }

    struct command command = commandList[parsedCommand.opcode];
    char *translationPattern = command.translationPattern;

    size_t patternLen = strlen(translationPattern);
    size_t strLen = patternLen;
    for(size_t i = 0; i < patternLen; i++) {
        char character = translationPattern[i];
        if(character >= '0' && character <= (char) command.usedParameters + 47) {
            char *parameter = parsedCommand.parameters[character - 48];
            strLen += strlen(parameter);
            if(parsedCommand.isPointer == (character - 48) + 1) {
                strLen += 2; // for [ and ]
            }
        }
    }

    char *translatedLine = malloc(strLen + 3); //Include an extra byte for the null-Pointer and two extra bytes in case []-brackets are needed for a pointer
    if(translatedLine == NULL) {
        fprintf(stderr, "Critical error: Memory allocation for command translation failed!");
        exit(EXIT_FAILURE);
    }
    translatedLine[0] = '\0';

    for(size_t i = 0; i < patternLen; i++) {
        char character = translationPattern[i];
        if(character >= '0' && character <= (char) command.usedParameters + 47) {
            char *parameter = parsedCommand.parameters[character - 48];

            if(parsedCommand.isPointer == (character - 48) + 1) {
                printDebugMessage("\tAppending pointer parameter", parameter, compileState -> logLevel);

                //Manually add braces to the string
                size_t currentStrLen = strlen(translatedLine);

                //Append a '['
                translatedLine[currentStrLen] = '[';
                translatedLine[currentStrLen + 1] = '\0';
                //Append the parameter
                strcat(translatedLine, parameter);
                //Append a ']'
                currentStrLen = strlen(translatedLine);
                translatedLine[currentStrLen] = ']';
                translatedLine[currentStrLen + 1] = '\0';
            } else {
                printDebugMessage("\tAppending parameter", parameter, compileState -> logLevel);
                strcat(translatedLine, parameter);
            }
        } else {
            char appendix[2] = {character, '\0'};
            strcat(translatedLine, appendix);
        }
    }

    printDebugMessage("\tWriting to file: ", translatedLine, compileState -> logLevel);
    if(parsedCommand.opcode != 0) {
        fprintf(outputFile, "\t");
    }
    fprintf(outputFile, "%s\n", translatedLine);

    printDebugMessage("\tDone, freeing memory", "", compileState -> logLevel);
    free(translatedLine);

    //Now, we need to insert more commands based on the current optimisation level
    if (compileState -> optimisationLevel == o_1) {
        //Insert a nop
        fprintf(outputFile, "\tnop\n");
    } else if (compileState -> optimisationLevel == o_2) {
        //Push and pop rax
        fprintf(outputFile, "\tpush rax\n\tpop rax\n");
    } else if (compileState -> optimisationLevel == o_3) {
        //Save and restore xmm0 on the stack using movups
        fprintf(outputFile, "\tmovups [rsp + 8], xmm0\n\tmovups xmm0, [rsp + 8]\n");
    } else if(compileState -> optimisationLevel == o42069) {
        //If we get here, then this was a function declaration. Insert a ret-statement and exit
        fprintf(outputFile, "\txor rax, rax\n\tret\n");
    }

    if(compileState -> useStabs && parsedCommand.opcode != 0) {
        //If this was a return statement and this is the end of file or a function definition is followed by it, we reached the end of the function. Define the label for the N_RBRAC stab
        if(parsedCommand.opcode > 0 && parsedCommand.opcode <= 3 && (compileState -> commandsArray.size == index + 1 || compileState -> commandsArray.arrayPointer[index + 1].opcode == 0)) {
            stabs_writeFunctionEndLabel(outputFile, translatorState);
        }
        //In any case, we now need to write the line info to the file
        if(!stabs_ignore(parsedCommand.opcode)) {
            stabs_writeLineInfo(outputFile, parsedCommand);
        }
    }
}

void writeToFile(struct compileState* compileState, char* inputFileString, FILE *outputFile) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    fprintf(outputFile, "#\n# Generated by the MemeAssembly compiler %s on %s#\n", version_string, asctime(&tm));
    fprintf(outputFile, ".intel_syntax noprefix\n");

    //Traverse the commandsArray to look for any functions
    for(size_t i = 0; i < compileState -> commandsArray.size; i++) {
        if(compileState -> commandsArray.arrayPointer[i].opcode == 0 && compileState -> commandsArray.arrayPointer[i].translate == 1) {
            //Write the function name with the prefix ".global" to the file
            fprintf(outputFile, ".global %s\n", compileState -> commandsArray.arrayPointer[i].parameters[0]);
        }
    }

    #ifdef WINDOWS
    //To interact with the Windows API, we need to reference the needed functions
    fprintf(outputFile, "\n.extern GetStdHandle\n.extern WriteFile\n.extern ReadFile\n");
    #endif

    #ifdef MACOS
    fprintf(outputFile, "\n.data\n\t");
    #else
    fprintf(outputFile, "\n.section .data\n\t");
    #endif

    fprintf(outputFile, ".LCharacter: .ascii \"a\"\n\t.Ltmp64: .byte 0, 0, 0, 0, 0, 0, 0, 0\n");

    //Write the file info if we are using stabs
    if(compileState -> useStabs) {
        stabs_writeFileInfo(outputFile, inputFileString);
    }

   
    #ifdef MACOS
    fprintf(outputFile, "\n\n.text\n\t");
    #else    
    fprintf(outputFile, "\n\n.section .text\n");
    #endif

    fprintf(outputFile, "\n\n.Ltext0:\n");

    struct translatorState translatorState = { NULL, 0 };

    for(size_t i = 0; i < compileState -> commandsArray.size; i++) {
        if(i == compileState -> commandsArray.randomIndex) {
            fprintf(outputFile, "\t.LConfusedStonks: ");
        }

        if(compileState -> commandsArray.arrayPointer[i].translate == 1) {
            printDebugMessageWithNumber("Translating Index:", (int) i, compileState -> logLevel);
            translateToAssembly(compileState, &translatorState, i, outputFile);
        }
    }

    //If the optimisation level is 42069, then this function will not be used as all commands are optimised out
    if(compileState -> optimisationLevel != o42069) {
        #ifdef WINDOWS
        //Using Windows API
        fprintf(outputFile,
                "\n\nwritechar:\n"
                "\tpush rcx\n"
                "\tpush rax\n"
                "\tpush rdx\n"
                "\tpush r8\n"
                "\tpush r9\n"
                //Get Handle of stdout
                "\tsub rsp, 32\n"
                "\tmov rcx, -11\n" //-11=stdout
                "\tcall GetStdHandle\n"//return value is in rax
                //Prepare the parameters for output
                "\tmov rcx, rax\n" //move Handle of stdout into rcx
                "\tlea rdx, [rip + .LCharacter]\n"
                "\tmov r8, 1\n" //Length of message = 1 character
                "\tlea r9, [rip + .Ltmp64]\n" //Number of bytes written, just discard that value
                "\tmov QWORD PTR [rsp + 32], 0\n"
                "\tcall WriteFile\n"
                "\tadd rsp, 32\n"

                //Restore all registers
                "\tpop r9\n"
                "\tpop r8\n"
                "\tpop rdx\n"
                "\tpop rax\n"
                "\tpop rcx\n"
                "\tret\n");

        fprintf(outputFile,
                "\n\nreadchar:\n"
                "\tpush rcx\n"
                "\tpush rax\n"
                "\tpush rdx\n"
                "\tpush r8\n"
                "\tpush r9\n"
                //Get Handle of stdin
                "\tsub rsp, 32\n"
                "\tmov rcx, -10\n" //-10=stdin
                "\tcall GetStdHandle\n"//return value is in rax
                //Prepare the parameters for reading from input
                "\tmov rcx, rax\n" //move Handle of stdin into rcx
                "\tlea rdx, [rip + .LCharacter]\n"
                "\tmov r8, 1\n" //Bytes to read = 1 character
                "\tlea r9, [rip + .Ltmp64]\n" //Number of bytes read, just discard that value
                //Parameter 5 and then 4 Bytes of emptiness on the stack
                "\tmov QWORD PTR [rsp + 32], 0\n"
                "\tcall ReadFile\n"
                "\tadd rsp, 32\n"

                //Restore all registers
                "\tpop r9\n"
                "\tpop r8\n"
                "\tpop rdx\n"
                "\tpop rax\n"
                "\tpop rcx\n"
                "\tret\n");
        #else
        //Using Linux syscalls
        fprintf(outputFile, "\n\nwritechar:\n\t"
                            "push rcx\n\t"
                            "push r11\n\t"
                            "push rax\n\t"
                            "push rdi\n\t"
                            "push rsi\n\t"
                            "push rdx\n\t"
                            "mov rdx, 1\n\t"
                            "lea rsi, [rip + .LCharacter]\n\t"
			    #ifdef LINUX
                            "mov rax, 1\n\t"
 			    #else
			    "mov rax, 0x2000004\n\t"
			    #endif
                            "syscall\n\t"
                            "pop rdx\n\t"
                            "pop rsi\n\t"
                            "pop rdi\n\t"
                            "pop rax\n\t"
                            "pop r11\n\t"
                            "pop rcx\n\t\n\t"
                            "ret\n");

        fprintf(outputFile, "\n\nreadchar:\n\t"
                            "push rcx\n\t"
                            "push r11\n\t"
                            "push rax\n\t"
                            "push rdi\n\t"
                            "push rsi\n\t"
                            "push rdx\n\n\t"
                            "mov rdx, 1\n\t"
                            "lea rsi, [rip + .LCharacter]\n\t"
                            "mov rdi, 0\n\t"
			    #ifdef LINUX
                            "mov rax, 0\n\t"
			    #else
			    "mov rax, 0x2000003\n\t"
			    #endif
                            "syscall\n\n\t"
                            "pop rdx\n\t"
                            "pop rsi\n\t"
                            "pop rdi\n\t"
                            "pop rax\n\t"
                            "pop r11\n\t"
                            "pop rcx\n\t"
                            "ret\n");
        #endif
    }

    //If we are using stabs, we now need to save all function info to the file
    if(compileState -> useStabs) {
        //Traverse the commandsArray to look for any functions
        for(size_t i = 0; i < compileState -> commandsArray.size; i++) {
            if(compileState -> commandsArray.arrayPointer[i].opcode == 0 && compileState -> commandsArray.arrayPointer[i].translate == 1) {
                //Write the stabs info
                stabs_writeFunctionInfo(outputFile, compileState -> commandsArray.arrayPointer[i].parameters[0]);
            }
        }

        fprintf(outputFile, "\n.LEOF:\n");
        fprintf(outputFile, ".stabs \"\", %d, 0, 0, .LEOF\n", N_SO);
    }

    if(compileState->optimisationLevel == o_s) {
        fprintf(outputFile, ".align 536870912\n");
    }
}
