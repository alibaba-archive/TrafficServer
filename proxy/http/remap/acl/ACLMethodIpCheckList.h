#ifndef _ACL_METHOD_IP_CHECK_LIST_H
#define _ACL_METHOD_IP_CHECK_LIST_H

#include "ACLChecker.h"
#include "DirectiveParams.h"
#include "ACLCheckList.h"

#define ACL_FILED_INDEX_METHOD          0
#define ACL_FILED_INDEX_SRC_IP          1
#define ACL_FILED_INDEX_WHOLE_METHOD_IP 2  //such as: acl allow all, acl check xxx

class ACLMethodIpCheckList : public ACLCheckList {
  public:
    ACLMethodIpCheckList();
    ~ACLMethodIpCheckList() {}

    int load(const DirectiveParams *parentParams);
    int check(const ACLContext & context);
};

#endif

