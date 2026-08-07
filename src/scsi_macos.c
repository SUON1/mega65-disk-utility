#include "m65/scsi_macos.h"

#include "m65/ufi.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/scsi/SCSITaskLib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SCSITaskDeviceInterface **device;
    bool exclusive;
} MacScsiContext;

static M65TransportStatus map_io_return(IOReturn result)
{
    if (result == kIOReturnSuccess) {
        return M65_TRANSPORT_OK;
    }
    if (result == kIOReturnNotPrivileged || result == kIOReturnNotPermitted) {
        return M65_TRANSPORT_PERMISSION;
    }
    if (result == kIOReturnNoDevice || result == kIOReturnOffline) {
        return M65_TRANSPORT_NO_DEVICE;
    }
    if (result == kIOReturnTimeout) {
        return M65_TRANSPORT_TIMEOUT;
    }
    return M65_TRANSPORT_IO;
}

static void describe_io_return(char *detail, size_t detail_size,
                               const char *operation, IOReturn result)
{
    if (detail != NULL && detail_size > 0U) {
        (void)snprintf(detail, detail_size, "%s failed with IOReturn 0x%08x",
                       operation, (unsigned int)result);
    }
}

static M65TransportStatus mac_acquire(M65Transport *transport,
                                      char *detail, size_t detail_size)
{
    MacScsiContext *context = (MacScsiContext *)transport->context;
    IOReturn result;
    if (context->exclusive) {
        return M65_TRANSPORT_OK;
    }
    result = (*context->device)->ObtainExclusiveAccess(context->device);
    if (result == kIOReturnSuccess) {
        context->exclusive = true;
        return M65_TRANSPORT_OK;
    }
    describe_io_return(detail, detail_size, "ObtainExclusiveAccess", result);
    if (map_io_return(result) == M65_TRANSPORT_PERMISSION &&
        detail != NULL && detail_size > 0U) {
        (void)snprintf(detail, detail_size,
                       "permission denied obtaining exclusive SCSI access; "
                       "rerun manually with sudo if you trust this diagnostic");
    }
    return map_io_return(result);
}

static M65TransportStatus mac_release(M65Transport *transport,
                                      char *detail, size_t detail_size)
{
    MacScsiContext *context = (MacScsiContext *)transport->context;
    IOReturn result;
    if (!context->exclusive) {
        return M65_TRANSPORT_OK;
    }
    result = (*context->device)->ReleaseExclusiveAccess(context->device);
    if (result == kIOReturnSuccess) {
        context->exclusive = false;
        return M65_TRANSPORT_OK;
    }
    describe_io_return(detail, detail_size, "ReleaseExclusiveAccess", result);
    return map_io_return(result);
}

static uint8_t transfer_direction(M65DataDirection direction)
{
    switch (direction) {
    case M65_DATA_NONE:
        return kSCSIDataTransfer_NoDataTransfer;
    case M65_DATA_IN:
        return kSCSIDataTransfer_FromTargetToInitiator;
    case M65_DATA_OUT:
        return kSCSIDataTransfer_FromInitiatorToTarget;
    }
    return kSCSIDataTransfer_NoDataTransfer;
}

static M65CommandResult mac_execute(M65Transport *transport, const M65Command *command)
{
    MacScsiContext *context = (MacScsiContext *)transport->context;
    M65CommandResult result;
    SCSITaskInterface **task = NULL;
    SCSITaskSGElement range;
    SCSI_Sense_Data sense;
    SCSITaskStatus task_status = kSCSITaskStatus_No_Status;
    UInt64 realized = 0U;
    IOReturn io_result;
    uint8_t cdb[16];
    char validation_detail[M65_MAX_ERROR_TEXT];

    (void)memset(&result, 0, sizeof(result));
    (void)memset(&sense, 0, sizeof(sense));
    if (!context->exclusive) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        (void)snprintf(result.detail, sizeof(result.detail),
                       "SCSI command attempted without exclusive access");
        return result;
    }
    if (!m65_validate_command(command, validation_detail, sizeof(validation_detail))) {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        (void)snprintf(result.detail, sizeof(result.detail), "%s", validation_detail);
        return result;
    }
    task = (*context->device)->CreateSCSITask(context->device);
    if (task == NULL) {
        result.transport_status = M65_TRANSPORT_IO;
        (void)snprintf(result.detail, sizeof(result.detail),
                       "CreateSCSITask returned NULL");
        return result;
    }
    (void)memcpy(cdb, command->cdb, command->cdb_length);
    io_result = (*task)->SetCommandDescriptorBlock(task, cdb,
                                                   (UInt8)command->cdb_length);
    if (io_result != kIOReturnSuccess) {
        describe_io_return(result.detail, sizeof(result.detail), "set CDB", io_result);
        result.transport_status = map_io_return(io_result);
        goto cleanup;
    }

    (void)memset(&range, 0, sizeof(range));
    if (command->direction == M65_DATA_NONE) {
        io_result = (*task)->SetScatterGatherEntries(
            task, NULL, 0U, 0U, kSCSIDataTransfer_NoDataTransfer);
    } else {
        range.address = (IOVirtualAddress)(uintptr_t)command->data;
        range.length = (IOByteCount)command->data_length;
        io_result = (*task)->SetScatterGatherEntries(
            task, &range, 1U, (UInt64)command->data_length,
            transfer_direction(command->direction));
    }
    if (io_result != kIOReturnSuccess) {
        describe_io_return(result.detail, sizeof(result.detail),
                           "set scatter/gather data", io_result);
        result.transport_status = map_io_return(io_result);
        goto cleanup;
    }
    io_result = (*task)->SetTimeoutDuration(task, (UInt32)command->timeout_ms);
    if (io_result != kIOReturnSuccess) {
        describe_io_return(result.detail, sizeof(result.detail), "set timeout", io_result);
        result.transport_status = map_io_return(io_result);
        goto cleanup;
    }

    io_result = (*task)->ExecuteTaskSync(task, &sense, &task_status, &realized);
    result.transport_status = map_io_return(io_result);
    result.scsi_status = (uint8_t)task_status;
    if (realized <= (UInt64)SIZE_MAX) {
        result.transferred = (size_t)realized;
    } else {
        result.transport_status = M65_TRANSPORT_PROTOCOL;
        (void)snprintf(result.detail, sizeof(result.detail),
                       "realized transfer count exceeds size_t");
    }
    (void)m65_parse_sense((const uint8_t *)(const void *)&sense, sizeof(sense),
                          &result.sense);
    if (io_result != kIOReturnSuccess) {
        describe_io_return(result.detail, sizeof(result.detail),
                           "ExecuteTaskSync", io_result);
    }

cleanup:
    if (task != NULL) {
        (void)(*task)->Release(task);
    }
    return result;
}

static void mac_destroy(M65Transport *transport)
{
    MacScsiContext *context;
    if (transport == NULL) {
        return;
    }
    context = (MacScsiContext *)transport->context;
    if (context != NULL) {
        if (context->exclusive) {
            (void)(*context->device)->ReleaseExclusiveAccess(context->device);
            context->exclusive = false;
        }
        if (context->device != NULL) {
            (void)(*context->device)->Release(context->device);
        }
        free(context);
    }
    free(transport);
}

static const M65TransportOps mac_ops = {
    mac_acquire,
    mac_execute,
    mac_release,
    mac_destroy
};

static const char *normalize_name(const char *bsd_name)
{
    const char *name = bsd_name;
    if (name != NULL && strncmp(name, "/dev/", 5U) == 0) {
        name += 5;
    }
    if (name != NULL && strncmp(name, "rdisk", 5U) == 0) {
        ++name;
    }
    return name;
}

static SCSITaskDeviceInterface **copy_task_interface(io_service_t media,
                                                     IOReturn *last_result)
{
    io_registry_entry_t current = media;
    SCSITaskDeviceInterface **device_interface = NULL;
    while (current != IO_OBJECT_NULL) {
        IOCFPlugInInterface **plugin = NULL;
        SInt32 score = 0;
        IOReturn create_result = IOCreatePlugInInterfaceForService(
            current, kIOSCSITaskDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
            &plugin, &score);
        *last_result = create_result;
        if (create_result == kIOReturnSuccess && plugin != NULL) {
            HRESULT query_result = (*plugin)->QueryInterface(
                plugin, CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID),
                (LPVOID *)(void *)&device_interface);
            (void)IODestroyPlugInInterface(plugin);
            if (query_result == S_OK && device_interface != NULL) {
                IOObjectRelease(current);
                return device_interface;
            }
        } else if (plugin != NULL) {
            (void)IODestroyPlugInInterface(plugin);
        }
        {
            io_registry_entry_t parent = IO_OBJECT_NULL;
            kern_return_t parent_result =
                IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent);
            IOObjectRelease(current);
            current = parent_result == KERN_SUCCESS ? parent : IO_OBJECT_NULL;
        }
    }
    return NULL;
}

M65Transport *m65_scsi_transport_create(const char *bsd_name,
                                        char *detail, size_t detail_size,
                                        M65TransportStatus *status)
{
    const char *name = normalize_name(bsd_name);
    CFMutableDictionaryRef matching;
    io_service_t media;
    SCSITaskDeviceInterface **device_interface;
    IOReturn last_result = kIOReturnUnsupported;
    M65Transport *transport;
    MacScsiContext *context;

    if (status != NULL) {
        *status = M65_TRANSPORT_IO;
    }
    if (name == NULL || name[0] == '\0') {
        if (detail != NULL && detail_size > 0U) {
            (void)snprintf(detail, detail_size, "missing BSD device name");
        }
        return NULL;
    }
    matching = IOBSDNameMatching(kIOMainPortDefault, 0U, name);
    if (matching == NULL) {
        return NULL;
    }
    media = IOServiceGetMatchingService(kIOMainPortDefault, matching);
    if (media == IO_OBJECT_NULL) {
        if (status != NULL) {
            *status = M65_TRANSPORT_NO_DEVICE;
        }
        if (detail != NULL && detail_size > 0U) {
            (void)snprintf(detail, detail_size,
                           "BSD device %s disappeared before SCSI transport setup", name);
        }
        return NULL;
    }
    device_interface = copy_task_interface(media, &last_result);
    if (device_interface == NULL) {
        M65TransportStatus mapped = map_io_return(last_result);
        if (mapped == M65_TRANSPORT_OK) {
            mapped = M65_TRANSPORT_IO;
        }
        if (status != NULL) {
            *status = mapped;
        }
        if (detail != NULL && detail_size > 0U) {
            (void)snprintf(detail, detail_size,
                           "no documented SCSITaskDeviceInterface is available for %s "
                           "(IOReturn 0x%08x)",
                           name, (unsigned int)last_result);
        }
        return NULL;
    }
    transport = (M65Transport *)calloc(1U, sizeof(*transport));
    context = (MacScsiContext *)calloc(1U, sizeof(*context));
    if (transport == NULL || context == NULL) {
        free(transport);
        free(context);
        (void)(*device_interface)->Release(device_interface);
        return NULL;
    }
    context->device = device_interface;
    transport->ops = &mac_ops;
    transport->context = context;
    if (status != NULL) {
        *status = M65_TRANSPORT_OK;
    }
    return transport;
}
