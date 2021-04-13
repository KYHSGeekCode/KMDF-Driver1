//
// 
// https://www.hackerschool.org/HS_Boards/data/Lib_kernel/1160630745/050104.pdf
// 작성자 김기홍 와우해커 세인트 시큐리티 대표이사
// www.wowhacker.com
// www.stsc.co.kr
// https://docs.microsoft.com/en-us/previous-versions/windows/hardware/network/ff562312(v%3Dvs.85)
// https://docs.microsoft.com/en-us/previous-versions/windows/hardware/network/ff548976(v=vs.85)
// https://pastebin.com/tCHqNnJH

#include "IPFilter.h"

SINGLE_LIST_ENTRY ipListHead;

void PushIpFilterEntry(PSINGLE_LIST_ENTRY ListHead, PIP_FILTER_ENTRY Entry) {
    PushEntryList(&ipListHead, &(Entry->SingleListEntry));
}

PIP_FILTER_ENTRY PopIpFilterEntry(PSINGLE_LIST_ENTRY ListHead) {
    PSINGLE_LIST_ENTRY SingleListEntry;
    SingleListEntry = PopEntryList(&ipListHead);
    return CONTAINING_RECORD(SingleListEntry, IP_FILTER_ENTRY, SingleListEntry);
}

NTSTATUS AddFilterToList(UINT32 srcAddr, UINT32 destAddr) {
    PIP_FILTER_ENTRY allocated = ExAllocatePoolZero(POOL_COLD_ALLOCATION, sizeof(IP_FILTER_ENTRY), "ll");
    allocated->srcAddr = srcAddr;
    allocated->destAddr = destAddr;
    PushIpFilterEntry(&ipListHead, allocated);
}

VOID ClearFilters() {
    PIP_FILTER_ENTRY entry = NULL;
    while (entry = PopIpFilterEntry(&ipListHead) != NULL) {
        ExFreePool(entry);
    }
}


NTSTATUS SetFilterFunction(PacketFilterExtensionPtr filterFunction) {
    NTSTATUS status = STATUS_SUCCESS, waitStatus = STATUS_SUCCESS;
    UNICODE_STRING filterName;
    PDEVICE_OBJECT ipDeviceObject = NULL;
    PFILE_OBJECT ipFileObject = NULL;
    PF_SET_EXTENSION_HOOK_INFO filterData;
    KEVENT event;
    IO_STATUS_BLOCK ioStatus;
    PIRP irp;
    // IpFilterDriver . 장치의 포인터를 가져온다
    RtlInitUnicodeString(&filterName, DD_IPFLTRDRVR_DEVICE_NAME);
    status = IoGetDeviceObjectPointer(&filterName, STANDARD_RIGHTS_ALL, &ipFileObject, &ipDeviceObject);
    if (NT_SUCCESS(status)) {
        // 함수의 인자들과 함께 구조체를 초기화 시켜준다
        filterData.ExtensionPointer = filterFunction;
        // 이벤트를 초기화 시켜준다
        // IpFilterDriver 그러면 로부터 이벤트가 세팅되면
        // 우리는 작업 완료
        KeInitializeEvent(&event, NotificationEvent, FALSE);
        // irp를 만들어서 필터 함수를 내보낼 수 있도록 한다
        irp = IoBuildDeviceIoControlRequest(IOCTL_PF_SET_EXTENSION_POINTER, ipDeviceObject, NULL, 0, NULL, 0, FALSE, &event, &ioStatus);
        if (irp != NULL) {
            // irp sent
            status = IoCallDriver(ipDeviceObject, irp);
            // IpFilterDriver wait
            if (status == STATUS_PENDING) {
                waitStatus = KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
                if (waitStatus != STATUS_SUCCESS)
                    ;
                // 에러
            }
            status = ioStatus.Status;
            if (!NT_SUCCESS(status))
                ;
            // 에러
        } else {
            // 실패할 경우 에러를 리턴한다
            status = STATUS_INSUFFICIENT_RESOURCES;
            // IRP 생성 오류
        }
        if (ipFileObject != NULL)
            ObDereferenceObject(ipFileObject);
        ipFileObject = NULL;
        ipDeviceObject = NULL;
    } else
        // 포인터 가져오는 동안 오류
        return status;
}

PF_FORWARD_ACTION match_by_addr(UINT32 srcAddr, UINT32 destAddr) {
    PIP_FILTER_ENTRY entry = CONTAINING_RECORD(&ipListHead, IP_FILTER_ENTRY, SingleListEntry);
    while (entry != NULL) {
        if (entry->srcAddr == srcAddr && entry->destAddr == destAddr) {
            return PF_DROP;
        }
        if (entry->srcAddr == -1 && entry->destAddr == destAddr) {
            return PF_DROP;
        }
        if (entry->destAddr == -1 && entry->srcAddr == srcAddr) {
            return PF_DROP;
        }
        if (entry->destAddr == -1 && entry->srcAddr == -1) {
            return PF_DROP;
        }
        entry = CONTAINING_RECORD(entry->SingleListEntry.Next, IP_FILTER_ENTRY, SingleListEntry);
    }
    return PF_FORWARD;
}


NTSTATUS MyPacketFilterExtension(
// IP 패킷 헤더
        IN unsigned char *PacketHeader,
// 헤더를 포함하지 않는 패킷
        IN unsigned char *Packet,
// IP 패킷 헤더의 길이를 제외한 패킷 길이
        IN unsigned int PacketLength,
// ( ) 장치 인덱스 몇 번째 장치 인지
// 받은 패킷에 대해서
        IN unsigned int RecvInterfaceIndex,
// ( ) 장치 인덱스 몇 번째 장치 인지
// 보내는 패킷에 대해서
        IN unsigned int SendInterfaceIndex,
// IP 주소 형태
// 장치가 받은 주소
        IN IPAddr RecvLinkNextHop,
// IP 주소 형태
// 장치가 보낼 주소
        IN IPAddr SendLinkNextHop
) {
    PIPV4_HEADER pipv4Header = (PIPV4_HEADER) PacketHeader;
    int srcAddr = pipv4Header->SourceAddress;
    int destAddr = pipv4Header->DestinationAddress;
    int type = pipv4Header->TypeOfService;
    if (type == 1) { // ICMP
        PICMP_HEADER picmpHeader = (PICMP_HEADER) Packet;
        // 딱히 파싱 필요 없어보임
    } else if (type == 6) { // TCP
        PTCP_HEADER ptcpHeader = (PTCP_HEADER) Packet;
        int dataOffset = (unsigned char) (ptcpHeader->offset_ns >> 4);
        char *realData = ((char *) ptcpHeader) + dataOffset;
        // cut until packet length and log it
    } else if (type == 17) { // UDP
        PUDP_HEADER pudpHeader = (PUDP_HEADER) Packet;
        char *realData = ((char *) pudpHeader) + 8;
        // cut until packet length and log it
    }

    NTSTATUS result;
    if (result = match_by_addr(srcAddr, destAddr) != PF_PASS) {
        return result;
    }
    return PF_FORWARD;
}

// typedef struct IPHeader { UCHAR iph_verlen; UCHAR iph_tos; USHORT iph_length;
//        USHORT iph_id; USHORT iph_offset; UCHAR iph_ttl; UCHAR iph_protocol; USHORT iph_xsum; ULONG iph_src;
//        ULONG iph_dest; } IPHeader;

// typedef PF_FORWARD_ACTION (*PacketFilterExtensionPtr)(
//// IP 패킷 헤더
//IN unsigned char *PacketHeader,
//// 헤더를 포함하지 않는 패킷
//IN unsigned char *Packet,
//// IP 패킷 헤더의 길이를 제외한 패킷 길이
//IN unsigned int PacketLength,
//// ( ) 장치 인덱스 몇 번째 장치 인지
//// 받은 패킷에 대해서
//IN unsigned int RecvInterfaceIndex,
//// ( ) 장치 인덱스 몇 번째 장치 인지
//// 보내는 패킷에 대해서
//IN unsigned int SendInterfaceIndex,
//// IP 주소 형태
//// 장치가 받은 주소
//IN IPAddr RecvLinkNextHop,
//// IP 주소 형태
//// 장치가 보낼 주소
//IN IPAddr SendLinkNextHop
//);

// PF_FORWARD_ACTION . 은 아래와 같은 결과 값을 가질 수 있게 된다
//• PF_FORWARD
//패킷을 정상적으로 처리 하기 위해 시스템 상의 에 값을 넣는다 넣게 되면 해 IP Stack .
//당 패킷은 처리를 하기 위한 어플리케이션으로 넘어가게 되며 해당 어플리케이션에서는 받
//은 정보를 가지고 적절한 처리를 하게 된다.
//• PF_DROP
//패킷을 드롭 하게 된다 시스템 상의 에 해당 포인터를 넘겨 주지 않고 폐기를
//함으로써 어플리케이션은 해당 패킷을 받지 못하게 된다.
//• PF_PASS
//패킷을 그냥 통과 시킨다 에 넣지는 않지만 시스템 드라이버 내부는 통과 하게 . IP Stack
//된다 하지만 에 값을 넣지 않기 때문에 어플리케이션에서는 정상적인 패킷 데이 . IP Stack
//터를 받지 못하는 것으로 나온다.

// https://www.daniweb.com/programming/software-development/threads/200708/how-to-convert-uint32-to-ip-address-dot-format
// okay, well before you get into the gory details of using winsock for network programming with C, you should understand the basic exercise of converting one number system to another number system, using bitwise operators.
//
//consider the address:
//10.1.10.127
//
//if you convert each octet to a binary group
//
//10         1        10       127
//00001010  00000001  00001010  01111111
//then the 32 bit binary number as one single integer has the decimal value.
//
//1010000000010000101001111111(bin) = 167840383(dec)
//
//you can programmatically convert between these number systems in either direction. consider (and understand) this snippet:
//
//unsigned int  ipAddress = 167840383;
//unsigned char octet[4]  = {0,0,0,0};
//
//for (i=0; i<4; i++)
//{
//    octet[i] = ( ipAddress >> (i*8) ) & 0xFF;
//}