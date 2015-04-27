#ifndef STRUCTDECL_H
#define STRUCTDECL_H

/*
struct entity
	x, y,
	w, h,
	name, type,
	radius
end

player = new entity # need to map variable player to entity structdecl

*/

#define MAX_MEMBERS	256

typedef struct sStructDecl
{
	char* name;
	char* memberNames[MAX_MEMBERS];
	int numMembers;
} StructDecl;

StructDecl* DeclareStruct(const char* name);
StructDecl* GetStructDecl(const char* name);

void AddMember(StructDecl* decl, const char* name);
int GetMemberIndex(StructDecl* decl, const char* name);
void

void DeleteStructDeclarations();

#endif
