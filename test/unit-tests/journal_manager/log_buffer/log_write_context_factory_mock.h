#include <gmock/gmock.h>

#include <list>
#include <string>
#include <vector>

#include "src/journal_manager/log_buffer/log_write_context_factory.h"

namespace pos
{
class MockLogWriteContextFactory : public LogWriteContextFactory
{
public:
    using LogWriteContextFactory::LogWriteContextFactory;
    MOCK_METHOD(void, Init, (JournalConfiguration * config), (override));
    MOCK_METHOD(LogWriteContext*, CreateBlockMapLogWriteContext, (VolumeIoSmartPtr volumeIo, EventSmartPtr callbackEvent), (override));
    MOCK_METHOD(LogWriteContext*, CreateStripeMapLogWriteContext, (StripeSmartPtr stripe, StripeAddr oldAddr, EventSmartPtr callbackEvent), (override));
    MOCK_METHOD(LogWriteContext*, CreateGcBlockMapLogWriteContext, (GcStripeMapUpdateList mapUpdates, EventSmartPtr callbackEvent), (override));
    MOCK_METHOD(std::vector<LogWriteContext*>, CreateGcBlockMapLogWriteContexts, (GcStripeMapUpdateList mapUpdates, EventSmartPtr callbackEvent), (override));
    MOCK_METHOD(LogWriteContext*, CreateGcStripeFlushedLogWriteContext, (GcStripeMapUpdateList mapUpdates, EventSmartPtr callbackEvent), (override));
    MOCK_METHOD(LogWriteContext*, CreateVolumeDeletedLogWriteContext, (int volId, uint64_t contextVersion, EventSmartPtr callback), (override));
};

} // namespace pos
