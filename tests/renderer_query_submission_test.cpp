#include "renderer_query_submission.h"
#include <cstdio>
int main(){
  outrun::QuerySubmissionBudget budget;
  int calls=0;
  auto success=[&]{++calls;return true;};
  if(budget.Try(0,false,success) || calls)return 1;
  if(!budget.Try(0,true,success) || calls!=1)return 2;
  if(budget.Try(0,true,success) || calls!=1)return 3;
  if(budget.Try(1,true,[&]{++calls;return false;}) || calls!=2)return 4;
  if(!budget.Try(1,true,success) || calls!=3)return 5;
  if(!budget.Try(2,true,success) || calls!=4)return 6;
  std::puts("PASS: unsafe scopes untouched, once per frame including frame0, retry after failed submission");
}
