/*
This file is part of the MemeAssembly compiler.

 Copyright © 2021 Tobias Kamm

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

#include "functions.h"
#include "../logger/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Creates a function struct by starting at the function definition and then traversing the
 * command array until a return statement, new function definition or end of array is found
 * @param commandsArray a pointer to the commands array created by the parser
 * @param functionStartAtIndex at which index of the array the function definition is
 * @param functionDeclarationOpcode the opcode of the function declaration command. The three return commands must be the three consecutive opcodes
 * @return a function struct containing all parsed information
 */
struct function parseFunction(struct compileState *compileState, size_t functionStartAtIndex, int functionDeclarationOpcode) {
    struct parsedCommand functionStart = *(compileState -> commandsArray.arrayPointer + functionStartAtIndex);

    //Define the structs
    struct function function;

    //Set the function name
    function.name = functionStart.parameters[0];

    //Set the line number
    function.definedInLine = (size_t) functionStart.lineNum;

    printDebugMessage("\tParsing function:", functionStart.parameters[0], compileState -> logLevel);

    size_t index = 1;
    size_t functionEndIndex = 0; //This points to the last found returns statement and is 0 if no return statement was found until now
    //Iterate through all commands until a return statement is found or the end of the array is reached
    while (functionStartAtIndex + index < compileState -> commandsArray.size) {
        struct parsedCommand parsedCommand = *(compileState -> commandsArray.arrayPointer + (functionStartAtIndex + index));
        //Get the opcode
        uint8_t opcode = parsedCommand.opcode;

        if(opcode == functionDeclarationOpcode) {
            //If there hasn't been a return statement until now, throw an error since there was no return statement until now
            if(functionEndIndex == 0) {
                printSemanticError("Expected a return statement, but got a new function definition", parsedCommand.lineNum, compileState);
            }
            break;
        } if(opcode > functionDeclarationOpcode && opcode <= functionDeclarationOpcode + 3) { //Function is a return statement
            functionEndIndex = index;
        }
        index++;
    }
    printDebugMessageWithNumber("\t\tIteration stopped at index", (int) index, compileState -> logLevel);

    if(functionEndIndex == 0) {
        printSemanticError("No return statement found", functionStart.lineNum, compileState);
    }
    function.numberOfCommands = functionEndIndex;
    return function;
}

/**
 * Checks if the function definitions are valid. This includes making sure that
 *  - no function names are used twice
 *  - no commands are outside of function definition
 *  - functions end with a return statement
 *  - there is a main function if it is supposed to be executable
 * @param commandsArray a pointer to the commandsArray created by the parser
 * @param functionDeclarationOpcode the opcode of the function declaration command. The three return commands must be the three consecutive opcodes
 */
void checkFunctionValidity(struct compileState* compileState, int functionDeclarationOpcode) {
    struct parsedCommand *arrayPointer = compileState -> commandsArray.arrayPointer;

    //First, count how many function definitions there are
    size_t functionDefinitions = 0;
    for (size_t i = 0; i < compileState -> commandsArray.size; ++i) {
        if(compileState -> commandsArray.arrayPointer[i].opcode == functionDeclarationOpcode) {
            functionDefinitions++;
        }
    }
    printDebugMessageWithNumber("Number of functions:", (int) functionDefinitions, compileState -> logLevel);

    //Now we create our array of functions
    int functionArrayIndex = 0;
    struct function *functions = calloc(sizeof(struct function), functionDefinitions);
    if(functions == NULL) {
        fprintf(stderr, "Critical error: Memory allocation for command parameter failed!");
        exit(EXIT_FAILURE);
    }

    printDebugMessage("Starting function parsing", "", compileState -> logLevel);

    //We now traverse the commands array again, this time parsing the functions
    size_t commandArrayIndex = 0; //At which command we currently are
    while (commandArrayIndex < compileState -> commandsArray.size) {
        for (; commandArrayIndex < compileState -> commandsArray.size; commandArrayIndex++) {
            //At this point, we are in between function definitions, so any commands that are not function definitions are illegal
            if(arrayPointer[commandArrayIndex].opcode != functionDeclarationOpcode) {
                printSemanticError("Statement does not belong to any function", compileState -> commandsArray.arrayPointer[commandArrayIndex].lineNum, compileState);
            } else {
                break;
            }
        }

        if(commandArrayIndex >= compileState -> commandsArray.size) {
            break;
        }

        //Parse the function
        functions[functionArrayIndex] = parseFunction(compileState, commandArrayIndex, functionDeclarationOpcode);
        //Increase our command index so that it points to the next unparsed command
        commandArrayIndex += functions[functionArrayIndex].numberOfCommands + 1;
        //Increase our function array index so that it points to the next uninitialised struct
        functionArrayIndex++;
    }

    /*
     * We now need to check the function names, specifically
     * - that no function names appeared twice
     * - that a main function exists (if checkForMainFunction is set to 1)
     */
    uint8_t mainFunctionExists = 0;
    char* mainFuncName =
	#ifdef MACOS
	"_main";
	#else
	"main";
	#endif

    for(size_t i = 0; i < functionDefinitions; i++) {
        struct function function = functions[i];

        if(strcmp(function.name, mainFuncName) == 0) {	
            mainFunctionExists = 1;
        }

        //We now traverse through all other function names and check if their names match. If so, print an error
        for(size_t j = i + 1; j < functionDefinitions; j++) {
            if(strcmp(function.name, functions[j].name) == 0) {
                printSemanticErrorWithExtraLineNumber("Duplicate function definition", (int) functions[j].definedInLine, (int) function.definedInLine, compileState);
            }
        }
    }

    if(compileState -> compileMode == executable && mainFunctionExists == 0) {
        printSemanticError("An executable cannot be created if no main-function exists", 1, compileState);
    }

    printDebugMessage("Checks done, freeing memory", "", compileState -> logLevel);
    //Now, we free all memory again
    //Free the allocated memory for the function array
    free(functions);
}
