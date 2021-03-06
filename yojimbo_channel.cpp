/*
    Yojimbo Network Library.
    
    Copyright © 2016 - 2017, The Network Protocol Company, Inc.
*/

#include "yojimbo_config.h"
#include "yojimbo_channel.h"
#include "yojimbo_platform.h"
#include "yojimbo_allocator.h"

namespace yojimbo
{
    void ChannelPacketData::Initialize()
    {
        channelIndex = 0;
        blockMessage = 0;
        messageFailedToSerialize = 0;
        message.numMessages = 0;
        initialized = 1;
    }

    void ChannelPacketData::Free( MessageFactory & messageFactory )
    {
        yojimbo_assert( initialized );

        Allocator & allocator = messageFactory.GetAllocator();

        if ( !blockMessage )
        {
            if ( message.numMessages > 0 )
            {
                for ( int i = 0; i < message.numMessages; ++i )
                {
                    if ( message.messages[i] )
                    {
                        messageFactory.ReleaseMessage( message.messages[i] );
                    }
                }

                YOJIMBO_FREE( allocator, message.messages );
            }
        }
        else
        {
            if ( block.message )
            {
                messageFactory.ReleaseMessage( block.message );
                block.message = NULL;
            }

            YOJIMBO_FREE( allocator, block.fragmentData );
        }

        initialized = 0;
    }

    template <typename Stream> bool SerializeOrderedMessages( Stream & stream, MessageFactory & messageFactory, int & numMessages, Message ** & messages, int maxMessagesPerPacket )
    {
        const int maxMessageType = messageFactory.GetNumTypes() - 1;

        bool hasMessages = Stream::IsWriting && numMessages != 0;

        serialize_bool( stream, hasMessages );

        if ( hasMessages )
        {
            serialize_int( stream, numMessages, 1, maxMessagesPerPacket );

            int * messageTypes = (int*) alloca( sizeof( int ) * numMessages );

            uint16_t * messageIds = (uint16_t*) alloca( sizeof( uint16_t ) * numMessages );

            memset( messageTypes, 0, sizeof( int ) * numMessages );
            memset( messageIds, 0, sizeof( uint16_t ) * numMessages );

            if ( Stream::IsWriting )
            {
                yojimbo_assert( messages );

                for ( int i = 0; i < numMessages; ++i )
                {
                    yojimbo_assert( messages[i] );
                    messageTypes[i] = messages[i]->GetType();
                    messageIds[i] = messages[i]->GetId();
                }
            }
            else
            {
                Allocator & allocator = messageFactory.GetAllocator();

                messages = (Message**) YOJIMBO_ALLOCATE( allocator, sizeof( Message* ) * numMessages );

                for ( int i = 0; i < numMessages; ++i )
                {
                    messages[i] = NULL;
                }
            }

            serialize_bits( stream, messageIds[0], 16 );

            for ( int i = 1; i < numMessages; ++i )
                serialize_sequence_relative( stream, messageIds[i-1], messageIds[i] );

            for ( int i = 0; i < numMessages; ++i )
            {
                if ( maxMessageType > 0 )
                {
                    serialize_int( stream, messageTypes[i], 0, maxMessageType );
                }
                else
                {
                    messageTypes[i] = 0;
                }

                if ( Stream::IsReading )
                {
                    messages[i] = messageFactory.CreateMessage( messageTypes[i] );

                    if ( !messages[i] )
                    {
                        yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to create message of type %d (SerializeOrderedMessages)\n", messageTypes[i] );
                        return false;
                    }

                    messages[i]->SetId( messageIds[i] );
                }

                yojimbo_assert( messages[i] );

                if ( !messages[i]->SerializeInternal( stream ) )
                {
                    yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to serialize message of type %d (SerializeOrderedMessages)\n", messageTypes[i] );
                    return false;
                }
            }
        }

        return true;
    }

    template <typename Stream> bool SerializeMessageBlock( Stream & stream, MessageFactory & messageFactory, BlockMessage * blockMessage, int maxBlockSize )
    {
        int blockSize = Stream::IsWriting ? blockMessage->GetBlockSize() : 0;

        serialize_int( stream, blockSize, 1, maxBlockSize );

        uint8_t * blockData;

        if ( Stream::IsReading )
        {
            Allocator & allocator = messageFactory.GetAllocator();
            blockData = (uint8_t*) YOJIMBO_ALLOCATE( allocator, blockSize );
            if ( !blockData )
            {
                yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to allocate message block (SerializeMessageBlock)\n" );
                return false;
            }
            blockMessage->AttachBlock( allocator, blockData, blockSize );
        }                   
        else
        {
            blockData = blockMessage->GetBlockData();
        } 

        serialize_bytes( stream, blockData, blockSize );

        return true;
    }

    template <typename Stream> bool SerializeUnorderedMessages( Stream & stream, MessageFactory & messageFactory, int & numMessages, Message ** & messages, int maxMessagesPerPacket, int maxBlockSize )
    {
        const int maxMessageType = messageFactory.GetNumTypes() - 1;

        bool hasMessages = Stream::IsWriting && numMessages != 0;

        serialize_bool( stream, hasMessages );

        if ( hasMessages )
        {
            serialize_int( stream, numMessages, 1, maxMessagesPerPacket );

            int * messageTypes = (int*) alloca( sizeof( int ) * numMessages );

            memset( messageTypes, 0, sizeof( int ) * numMessages );

            if ( Stream::IsWriting )
            {
                yojimbo_assert( messages );

                for ( int i = 0; i < numMessages; ++i )
                {
                    yojimbo_assert( messages[i] );
                    messageTypes[i] = messages[i]->GetType();
                }
            }
            else
            {
                Allocator & allocator = messageFactory.GetAllocator();

                messages = (Message**) YOJIMBO_ALLOCATE( allocator, sizeof( Message* ) * numMessages );

                for ( int i = 0; i < numMessages; ++i )
                    messages[i] = NULL;
            }

            for ( int i = 0; i < numMessages; ++i )
            {
                if ( maxMessageType > 0 )
                {
                    serialize_int( stream, messageTypes[i], 0, maxMessageType );
                }
                else
                {
                    messageTypes[i] = 0;
                }

                if ( Stream::IsReading )
                {
                    messages[i] = messageFactory.CreateMessage( messageTypes[i] );

                    if ( !messages[i] )
                    {
                        yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to create message type %d (SerializeUnorderedMessages)\n", messageTypes[i] );
                        return false;
                    }
                }

                yojimbo_assert( messages[i] );

                if ( !messages[i]->SerializeInternal( stream ) )
                {
                    yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to serialize message type %d (SerializeUnorderedMessages)\n", messageTypes[i] );
                    return false;
                }

                if ( messages[i]->IsBlockMessage() )
                {
                    BlockMessage * blockMessage = (BlockMessage*) messages[i];
                    if ( !SerializeMessageBlock( stream, messageFactory, blockMessage, maxBlockSize ) )
                    {
                        yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to serialize message block (SerializeUnorderedMessages)\n" );
                        return false;
                    }
                }
            }
        }

        return true;
    }

    template <typename Stream> bool SerializeBlockFragment( Stream & stream, MessageFactory & messageFactory, ChannelPacketData::BlockData & block, const ChannelConfig & channelConfig )
    {
        const int maxMessageType = messageFactory.GetNumTypes() - 1;

        serialize_bits( stream, block.messageId, 16 );

        if ( channelConfig.GetMaxFragmentsPerBlock() > 1 )
        {
            serialize_int( stream, block.numFragments, 1, channelConfig.GetMaxFragmentsPerBlock() );
        }
        else
        {
            if ( Stream::IsReading )
                block.numFragments = 1;
        }

        if ( block.numFragments > 1 )
        {
            serialize_int( stream, block.fragmentId, 0, block.numFragments - 1 );
        }
        else
        {
            if ( Stream::IsReading )
                block.fragmentId = 0;
        }

        serialize_int( stream, block.fragmentSize, 1, channelConfig.fragmentSize );

        if ( Stream::IsReading )
        {
            block.fragmentData = (uint8_t*) YOJIMBO_ALLOCATE( messageFactory.GetAllocator(), block.fragmentSize );

            if ( !block.fragmentData )
            {
                yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to serialize block fragment (SerializeBlockFragment)\n" );
                return false;
            }
        }

        serialize_bytes( stream, block.fragmentData, block.fragmentSize );

        if ( block.fragmentId == 0 )
        {
            // block message

            serialize_int( stream, block.messageType, 0, maxMessageType );

            if ( Stream::IsReading )
            {
                Message * message = messageFactory.CreateMessage( block.messageType );

                if ( !message )
                {
                    yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to create block message type %d (SerializeBlockFragment)\n", block.messageType );
                    return false;
                }

                if ( !message->IsBlockMessage() )
                {
                    yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: received block fragment attached to non-block message (SerializeBlockFragment)\n" );
                    return false;
                }

                block.message = (BlockMessage*) message;
            }

            yojimbo_assert( block.message );

            if ( !block.message->SerializeInternal( stream ) )
            {
                yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "error: failed to serialize block message of type %d (SerializeBlockFragment)\n", block.messageType );
                return false;
            }
        }
        else
        {
            if ( Stream::IsReading )
                block.message = NULL;
        }

        return true;
    }

    template <typename Stream> bool ChannelPacketData::Serialize( Stream & stream, MessageFactory & messageFactory, const ChannelConfig * channelConfigs, int numChannels )
    {
        yojimbo_assert( initialized );

#if YOJIMBO_DEBUG_MESSAGE_BUDGET
        int startBits = stream.GetBitsProcessed();
#endif // #if YOJIMBO_DEBUG_MESSAGE_BUDGET

        if ( numChannels > 1 )
            serialize_int( stream, channelIndex, 0, numChannels - 1 );
        else
            channelIndex = 0;

        const ChannelConfig & channelConfig = channelConfigs[channelIndex];

        serialize_bool( stream, blockMessage );

        if ( !blockMessage )
        {
            switch ( channelConfig.type )
            {
                case CHANNEL_TYPE_RELIABLE_ORDERED:
                {
                    if ( !SerializeOrderedMessages( stream, messageFactory, message.numMessages, message.messages, channelConfig.maxMessagesPerPacket ) )
                    {
                        messageFailedToSerialize = 1;
                        return true;
                    }
                }
                break;

                case CHANNEL_TYPE_UNRELIABLE_UNORDERED:
                {
                    if ( !SerializeUnorderedMessages( stream, messageFactory, message.numMessages, message.messages, channelConfig.maxMessagesPerPacket, channelConfig.maxBlockSize ) )
                    {
                        messageFailedToSerialize = 1;
                        return true;
                    }
                }
                break;
            }

#if YOJIMBO_DEBUG_MESSAGE_BUDGET
            if ( channelConfig.packetBudget > 0 )
            {
                yojimbo_assert( stream.GetBitsProcessed() - startBits <= channelConfig.packetBudget * 8 );
            }
#endif // #if YOJIMBO_DEBUG_MESSAGE_BUDGET
        }
        else
        {
            if ( channelConfig.disableBlocks )
                return false;

            if ( !SerializeBlockFragment( stream, messageFactory, block, channelConfig ) )
                return false;
        }

        return true;
    }

    bool ChannelPacketData::SerializeInternal( ReadStream & stream, MessageFactory & messageFactory, const ChannelConfig * channelConfigs, int numChannels )
    {
        return Serialize( stream, messageFactory, channelConfigs, numChannels );
    }

    bool ChannelPacketData::SerializeInternal( WriteStream & stream, MessageFactory & messageFactory, const ChannelConfig * channelConfigs, int numChannels )
    {
        return Serialize( stream, messageFactory, channelConfigs, numChannels );
    }

    bool ChannelPacketData::SerializeInternal( MeasureStream & stream, MessageFactory & messageFactory, const ChannelConfig * channelConfigs, int numChannels )
    {
        return Serialize( stream, messageFactory, channelConfigs, numChannels );
    }

    // ------------------------------------------------------------------------------------

    Channel::Channel( Allocator & allocator, MessageFactory & messageFactory, const ChannelConfig & config, int channelIndex, double time ) : m_config( config )
    {
        yojimbo_assert( channelIndex >= 0 );
        yojimbo_assert( channelIndex < MaxChannels );
        m_channelIndex = channelIndex;
        m_allocator = &allocator;
        m_messageFactory = &messageFactory;
        m_errorLevel = CHANNEL_ERROR_NONE;
        m_time = time;
        ResetCounters();
    }

    uint64_t Channel::GetCounter( int index ) const
    {
        yojimbo_assert( index >= 0 );
        yojimbo_assert( index < CHANNEL_COUNTER_NUM_COUNTERS );
        return m_counters[index];
    }

    void Channel::ResetCounters()
    { 
        memset( m_counters, 0, sizeof( m_counters ) ); 
    }

    int Channel::GetChannelIndex() const 
    { 
        return m_channelIndex;
    }

    void Channel::SetErrorLevel( ChannelErrorLevel errorLevel )
    {
        if ( errorLevel != m_errorLevel && errorLevel != CHANNEL_ERROR_NONE )
        {
            yojimbo_printf( YOJIMBO_LOG_LEVEL_ERROR, "channel went into error state: %s\n", GetChannelErrorString( errorLevel ) );
        }
        m_errorLevel = errorLevel;
    }

    ChannelErrorLevel Channel::GetErrorLevel() const
    {
        return m_errorLevel;
    }

    // ------------------------------------------------------------------------------------

    ReliableOrderedChannel::ReliableOrderedChannel( Allocator & allocator, MessageFactory & messageFactory, const ChannelConfig & config, int channelIndex, double time ) : Channel( allocator, messageFactory, config, channelIndex, time )
    {
        yojimbo_assert( config.type == CHANNEL_TYPE_RELIABLE_ORDERED );

        yojimbo_assert( ( 65536 % config.sendQueueSize ) == 0 );
        yojimbo_assert( ( 65536 % config.receiveQueueSize ) == 0 );
        yojimbo_assert( ( 65536 % config.sentPacketBufferSize ) == 0 );

        m_sentPackets = YOJIMBO_NEW( *m_allocator, SequenceBuffer<SentPacketEntry>, *m_allocator, m_config.sentPacketBufferSize );
        
        m_messageSendQueue = YOJIMBO_NEW( *m_allocator, SequenceBuffer<MessageSendQueueEntry>, *m_allocator, m_config.sendQueueSize );
        
        m_messageReceiveQueue = YOJIMBO_NEW( *m_allocator, SequenceBuffer<MessageReceiveQueueEntry>, *m_allocator, m_config.receiveQueueSize );
        
        m_sentPacketMessageIds = (uint16_t*) YOJIMBO_ALLOCATE( *m_allocator, sizeof( uint16_t ) * m_config.maxMessagesPerPacket * m_config.sentPacketBufferSize );

        if ( !config.disableBlocks )
        {
            m_sendBlock = YOJIMBO_NEW( *m_allocator, SendBlockData, *m_allocator, m_config.maxBlockSize, m_config.GetMaxFragmentsPerBlock() );
            
            m_receiveBlock = YOJIMBO_NEW( *m_allocator, ReceiveBlockData, *m_allocator, m_config.maxBlockSize, m_config.GetMaxFragmentsPerBlock() );
        }
        else
        {
            m_sendBlock = NULL;
            m_receiveBlock = NULL;
        }

        Reset();
    }

    ReliableOrderedChannel::~ReliableOrderedChannel()
    {
        Reset();

        YOJIMBO_DELETE( *m_allocator, SendBlockData, m_sendBlock );
        YOJIMBO_DELETE( *m_allocator, ReceiveBlockData, m_receiveBlock );
        YOJIMBO_DELETE( *m_allocator, SequenceBuffer<SentPacketEntry>, m_sentPackets );
        YOJIMBO_DELETE( *m_allocator, SequenceBuffer<MessageSendQueueEntry>, m_messageSendQueue );
        YOJIMBO_DELETE( *m_allocator, SequenceBuffer<MessageReceiveQueueEntry>, m_messageReceiveQueue );
        
        YOJIMBO_FREE( *m_allocator, m_sentPacketMessageIds );

        m_sentPacketMessageIds = NULL;
    }

    void ReliableOrderedChannel::Reset()
    {
        SetErrorLevel( CHANNEL_ERROR_NONE );

        m_sendMessageId = 0;
        m_receiveMessageId = 0;
        m_oldestUnackedMessageId = 0;

        for ( int i = 0; i < m_messageSendQueue->GetSize(); ++i )
        {
            MessageSendQueueEntry * entry = m_messageSendQueue->GetAtIndex( i );
            if ( entry && entry->message )
                m_messageFactory->ReleaseMessage( entry->message );
        }

        for ( int i = 0; i < m_messageReceiveQueue->GetSize(); ++i )
        {
            MessageReceiveQueueEntry * entry = m_messageReceiveQueue->GetAtIndex( i );
            if ( entry && entry->message )
                m_messageFactory->ReleaseMessage( entry->message );
        }

        m_sentPackets->Reset();
        m_messageSendQueue->Reset();
        m_messageReceiveQueue->Reset();

        if ( m_sendBlock )
        {
            m_sendBlock->Reset();
        }

        if ( m_receiveBlock )
        {
            m_receiveBlock->Reset();
            if ( m_receiveBlock->blockMessage )
            {
                m_messageFactory->ReleaseMessage( m_receiveBlock->blockMessage );
                m_receiveBlock->blockMessage = NULL;
            }
        }

        ResetCounters();
    }

    bool ReliableOrderedChannel::CanSendMessage() const
    {
        yojimbo_assert( m_messageSendQueue );
        return m_messageSendQueue->Available( m_sendMessageId );
    }

    void ReliableOrderedChannel::SendMessage( Message * message )
    {
        yojimbo_assert( message );
        
        yojimbo_assert( CanSendMessage() );

        if ( GetErrorLevel() != CHANNEL_ERROR_NONE )
        {
            m_messageFactory->ReleaseMessage( message );
            return;
        }

        if ( !CanSendMessage() )
        {
            // Increase your send queue size!
            SetErrorLevel( CHANNEL_ERROR_SEND_QUEUE_FULL );
            m_messageFactory->ReleaseMessage( message );
            return;
        }

        yojimbo_assert( !( message->IsBlockMessage() && m_config.disableBlocks ) );

        if ( message->IsBlockMessage() && m_config.disableBlocks )
        {
            // You tried to send a block message, but block messages are disabled for this channel.
            SetErrorLevel( CHANNEL_ERROR_BLOCKS_DISABLED );
            m_messageFactory->ReleaseMessage( message );
            return;
        }

        message->SetId( m_sendMessageId );

        MessageSendQueueEntry * entry = m_messageSendQueue->Insert( m_sendMessageId );

        yojimbo_assert( entry );

        entry->block = message->IsBlockMessage();
        entry->message = message;
        entry->measuredBits = 0;
        entry->timeLastSent = -1.0;

        if ( message->IsBlockMessage() )
        {
            yojimbo_assert( ((BlockMessage*)message)->GetBlockSize() > 0 );
            yojimbo_assert( ((BlockMessage*)message)->GetBlockSize() <= m_config.maxBlockSize );
        }

        MeasureStream measureStream( m_messageFactory->GetAllocator() );

        message->SerializeInternal( measureStream );

        entry->measuredBits = measureStream.GetBitsProcessed();

        m_counters[CHANNEL_COUNTER_MESSAGES_SENT]++;

        m_sendMessageId++;
    }

    Message * ReliableOrderedChannel::ReceiveMessage()
    {
        if ( GetErrorLevel() != CHANNEL_ERROR_NONE )
            return NULL;

        MessageReceiveQueueEntry * entry = m_messageReceiveQueue->Find( m_receiveMessageId );
        if ( !entry )
            return NULL;

        Message * message = entry->message;

        yojimbo_assert( message );
        yojimbo_assert( message->GetId() == m_receiveMessageId );

        m_messageReceiveQueue->Remove( m_receiveMessageId );

        m_counters[CHANNEL_COUNTER_MESSAGES_RECEIVED]++;

        m_receiveMessageId++;

        return message;
    }

    void ReliableOrderedChannel::AdvanceTime( double time )
    {
        m_time = time;
    }
    
    int ReliableOrderedChannel::GetPacketData( ChannelPacketData & packetData, uint16_t packetSequence, int availableBits )
    {
        if ( !HasMessagesToSend() )
            return 0;

        if ( SendingBlockMessage() )
        {
            uint16_t messageId;
            uint16_t fragmentId;
            int fragmentBytes;
            int numFragments;
            int messageType;

            uint8_t * fragmentData = GetFragmentToSend( messageId, fragmentId, fragmentBytes, numFragments, messageType );

            if ( fragmentData )
            {
                const int fragmentBits = GetFragmentPacketData( packetData, messageId, fragmentId, fragmentData, fragmentBytes, numFragments, messageType );

                AddFragmentPacketEntry( messageId, fragmentId, packetSequence );

                return fragmentBits;
            }
        }
        else
        {
            int numMessageIds = 0;

            uint16_t * messageIds = (uint16_t*) alloca( m_config.maxMessagesPerPacket * sizeof( uint16_t ) );
            
            const int messageBits = GetMessagesToSend( messageIds, numMessageIds, availableBits );

            if ( numMessageIds > 0 )
            {
                GetMessagePacketData( packetData, messageIds, numMessageIds );

                AddMessagePacketEntry( messageIds, numMessageIds, packetSequence );

                return messageBits;
            }
        }

        return 0;
    }

    bool ReliableOrderedChannel::HasMessagesToSend() const
    {
        return m_oldestUnackedMessageId != m_sendMessageId;
    }

    int ReliableOrderedChannel::GetMessagesToSend( uint16_t * messageIds, int & numMessageIds, int availableBits )
    {
        yojimbo_assert( HasMessagesToSend() );

        numMessageIds = 0;

        if ( m_config.packetBudget > 0 )
            availableBits = yojimbo_min( m_config.packetBudget * 8, availableBits );

        const int giveUpBits = 4 * 8;

        const int messageTypeBits = bits_required( 0, m_messageFactory->GetNumTypes() - 1 );

        const int messageLimit = yojimbo_min( m_config.sendQueueSize, m_config.receiveQueueSize );

        uint16_t previousMessageId = 0;

        int usedBits = ConservativeMessageHeaderEstimate;

        int giveUpCounter = 0;

        for ( int i = 0; i < messageLimit; ++i )
        {
            if ( availableBits - usedBits < giveUpBits )
                break;

            if ( giveUpCounter > m_config.sendQueueSize )
                break;

            uint16_t messageId = m_oldestUnackedMessageId + i;

            MessageSendQueueEntry * entry = m_messageSendQueue->Find( messageId );

            if ( !entry )
                continue;

            if ( entry->block )
                break;
            
            if ( entry->timeLastSent + m_config.messageResendTime <= m_time && availableBits >= (int) entry->measuredBits )
            {                
                int messageBits = entry->measuredBits + messageTypeBits;
                
                if ( numMessageIds == 0 )
                {
                    messageBits += 16;
                }
                else
                {
                    MeasureStream stream( GetDefaultAllocator() );
                    serialize_sequence_relative_internal( stream, previousMessageId, messageId );
                    messageBits += stream.GetBitsProcessed();
                }

                if ( usedBits + messageBits > availableBits )
                {
                    giveUpCounter++;
                    continue;
                }

                usedBits += messageBits;

                messageIds[numMessageIds++] = messageId;
                
                entry->timeLastSent = m_time;

                previousMessageId = messageId;
            }

            if ( numMessageIds == m_config.maxMessagesPerPacket )
                break;
        }

        return usedBits;
    }

    void ReliableOrderedChannel::GetMessagePacketData( ChannelPacketData & packetData, const uint16_t * messageIds, int numMessageIds )
    {
        yojimbo_assert( messageIds );

        packetData.Initialize();
        packetData.channelIndex = GetChannelIndex();
        packetData.message.numMessages = numMessageIds;
        
        if ( numMessageIds == 0 )
            return;

        packetData.message.messages = (Message**) YOJIMBO_ALLOCATE( m_messageFactory->GetAllocator(), sizeof( Message* ) * numMessageIds );

        for ( int i = 0; i < numMessageIds; ++i )
        {
            MessageSendQueueEntry * entry = m_messageSendQueue->Find( messageIds[i] );
            yojimbo_assert( entry );
            yojimbo_assert( entry->message );
            yojimbo_assert( entry->message->GetRefCount() > 0 );
            packetData.message.messages[i] = entry->message;
            m_messageFactory->AcquireMessage( packetData.message.messages[i] );
        }
    }

    void ReliableOrderedChannel::AddMessagePacketEntry( const uint16_t * messageIds, int numMessageIds, uint16_t sequence )
    {
        SentPacketEntry * sentPacket = m_sentPackets->Insert( sequence );
        yojimbo_assert( sentPacket );
        if ( sentPacket )
        {
            sentPacket->acked = 0;
            sentPacket->block = 0;
            sentPacket->timeSent = m_time;
            sentPacket->messageIds = &m_sentPacketMessageIds[ ( sequence % m_config.sentPacketBufferSize ) * m_config.maxMessagesPerPacket ];
            sentPacket->numMessageIds = numMessageIds;            
            for ( int i = 0; i < numMessageIds; ++i )
            {
                sentPacket->messageIds[i] = messageIds[i];
            }
        }
    }

    void ReliableOrderedChannel::ProcessPacketMessages( int numMessages, Message ** messages )
    {
        const uint16_t minMessageId = m_receiveMessageId;
        const uint16_t maxMessageId = m_receiveMessageId + m_config.receiveQueueSize - 1;

        for ( int i = 0; i < (int) numMessages; ++i )
        {
            Message * message = messages[i];

            yojimbo_assert( message );  

            const uint16_t messageId = message->GetId();

            if ( sequence_less_than( messageId, minMessageId ) )
                continue;

            if ( sequence_greater_than( messageId, maxMessageId ) )
            {
                // Did you forget to dequeue messages on the receiver?
                SetErrorLevel( CHANNEL_ERROR_DESYNC );
                return;
            }

            if ( m_messageReceiveQueue->Find( messageId ) )
                continue;

            yojimbo_assert( !m_messageReceiveQueue->GetAtIndex( m_messageReceiveQueue->GetIndex( messageId ) ) );

            MessageReceiveQueueEntry * entry = m_messageReceiveQueue->Insert( messageId );
            if ( !entry )
            {
                // For some reason we can't insert the message in the receive queue
                SetErrorLevel( CHANNEL_ERROR_DESYNC );
                return;
            }

            entry->message = message;

            m_messageFactory->AcquireMessage( message );
        }
    }

    void ReliableOrderedChannel::ProcessPacketData( const ChannelPacketData & packetData, uint16_t packetSequence )
    {
        if ( m_errorLevel != CHANNEL_ERROR_NONE )
            return;
        
        if ( packetData.messageFailedToSerialize )
        {
            // A message failed to serialize read for some reason, eg. mismatched read/write.
            SetErrorLevel( CHANNEL_ERROR_FAILED_TO_SERIALIZE );
            return;
        }

        (void)packetSequence;

        if ( packetData.blockMessage )
        {
            ProcessPacketFragment( packetData.block.messageType, packetData.block.messageId, packetData.block.numFragments, packetData.block.fragmentId, packetData.block.fragmentData, packetData.block.fragmentSize, packetData.block.message );
        }
        else
        {
            ProcessPacketMessages( packetData.message.numMessages, packetData.message.messages );
        }
    }

    void ReliableOrderedChannel::ProcessAck( uint16_t ack )
    {
        // todo
        // printf( "%p: process ack %d\n", this, ack );

        SentPacketEntry * sentPacketEntry = m_sentPackets->Find( ack );
        if ( !sentPacketEntry )
            return;

        yojimbo_assert( !sentPacketEntry->acked );

        for ( int i = 0; i < (int) sentPacketEntry->numMessageIds; ++i )
        {
            const uint16_t messageId = sentPacketEntry->messageIds[i];
            MessageSendQueueEntry * sendQueueEntry = m_messageSendQueue->Find( messageId );
            if ( sendQueueEntry )
            {
                yojimbo_assert( sendQueueEntry->message );
                yojimbo_assert( sendQueueEntry->message->GetId() == messageId );
                m_messageFactory->ReleaseMessage( sendQueueEntry->message );
                m_messageSendQueue->Remove( messageId );
                UpdateOldestUnackedMessageId();
            }
        }

        if ( !m_config.disableBlocks && sentPacketEntry->block && m_sendBlock->active && m_sendBlock->blockMessageId == sentPacketEntry->blockMessageId )
        {        
            const int messageId = sentPacketEntry->blockMessageId;
            const int fragmentId = sentPacketEntry->blockFragmentId;

            // todo
            //printf( "%p: process ack for block message: ack = %d, messageId = %d, fragmentId = %d\n", this, ack, messageId, fragmentId );

            if ( !m_sendBlock->ackedFragment->GetBit( fragmentId ) )
            {
                m_sendBlock->ackedFragment->SetBit( fragmentId );
                m_sendBlock->numAckedFragments++;
                if ( m_sendBlock->numAckedFragments == m_sendBlock->numFragments )
                {
                    m_sendBlock->active = false;
                    MessageSendQueueEntry * sendQueueEntry = m_messageSendQueue->Find( messageId );
                    yojimbo_assert( sendQueueEntry );
                    m_messageFactory->ReleaseMessage( sendQueueEntry->message );
                    m_messageSendQueue->Remove( messageId );
                    UpdateOldestUnackedMessageId();
                }
            }
        }
    }

    void ReliableOrderedChannel::UpdateOldestUnackedMessageId()
    {
        const uint16_t stopMessageId = m_messageSendQueue->GetSequence();

        while ( true )
        {
            if ( m_oldestUnackedMessageId == stopMessageId || m_messageSendQueue->Find( m_oldestUnackedMessageId ) )
            {
                break;
            }
            ++m_oldestUnackedMessageId;
        }

        yojimbo_assert( !sequence_greater_than( m_oldestUnackedMessageId, stopMessageId ) );
    }

    bool ReliableOrderedChannel::SendingBlockMessage()
    {
        yojimbo_assert( HasMessagesToSend() );

        MessageSendQueueEntry * entry = m_messageSendQueue->Find( m_oldestUnackedMessageId );

        return entry ? entry->block : false;
    }

    uint8_t * ReliableOrderedChannel::GetFragmentToSend( uint16_t & messageId, uint16_t & fragmentId, int & fragmentBytes, int & numFragments, int & messageType )
    {
        MessageSendQueueEntry * entry = m_messageSendQueue->Find( m_oldestUnackedMessageId );

        yojimbo_assert( entry );
        yojimbo_assert( entry->block );

        BlockMessage * blockMessage = (BlockMessage*) entry->message;

        yojimbo_assert( blockMessage );

        messageId = blockMessage->GetId();

        const int blockSize = blockMessage->GetBlockSize();

        if ( !m_sendBlock->active )
        {
            // start sending this block

            // todo
            //printf( "%p: start sending block: messageId = %d\n", this, messageId );

            m_sendBlock->active = true;
            m_sendBlock->blockSize = blockSize;
            m_sendBlock->blockMessageId = messageId;
            m_sendBlock->numFragments = (int) ceil( blockSize / float( m_config.fragmentSize ) );
            m_sendBlock->numAckedFragments = 0;

            const int MaxFragmentsPerBlock = m_config.GetMaxFragmentsPerBlock();

            yojimbo_assert( m_sendBlock->numFragments > 0 );
            yojimbo_assert( m_sendBlock->numFragments <= MaxFragmentsPerBlock );

            m_sendBlock->ackedFragment->Clear();

            for ( int i = 0; i < MaxFragmentsPerBlock; ++i )
                m_sendBlock->fragmentSendTime[i] = -1.0;
        }

        numFragments = m_sendBlock->numFragments;

        // find the next fragment to send (there may not be one)

        fragmentId = 0xFFFF;

        for ( int i = 0; i < m_sendBlock->numFragments; ++i )
        {
            if ( !m_sendBlock->ackedFragment->GetBit( i ) && m_sendBlock->fragmentSendTime[i] + m_config.fragmentResendTime < m_time )
            {
                fragmentId = uint16_t( i );
                break;
            }
        }

        if ( fragmentId == 0xFFFF )
            return NULL;

        // allocate and return a copy of the fragment data

        messageType = blockMessage->GetType();

        fragmentBytes = m_config.fragmentSize;
        
        const int fragmentRemainder = blockSize % m_config.fragmentSize;

        if ( fragmentRemainder && fragmentId == m_sendBlock->numFragments - 1 )
            fragmentBytes = fragmentRemainder;

        uint8_t * fragmentData = (uint8_t*) YOJIMBO_ALLOCATE( m_messageFactory->GetAllocator(), fragmentBytes );

        if ( fragmentData )
        {
            memcpy( fragmentData, blockMessage->GetBlockData() + fragmentId * m_config.fragmentSize, fragmentBytes );

            m_sendBlock->fragmentSendTime[fragmentId] = m_time;
        }

        // todo
        //printf( "%p: send block fragment: messageId = %d, fragmentId = %d\n", this, messageId, fragmentId );

        return fragmentData;
    }

    int ReliableOrderedChannel::GetFragmentPacketData( ChannelPacketData & packetData, uint16_t messageId, uint16_t fragmentId, uint8_t * fragmentData, int fragmentSize, int numFragments, int messageType )
    {
        packetData.Initialize();

        packetData.channelIndex = GetChannelIndex();

        packetData.blockMessage = 1;

        packetData.block.fragmentData = fragmentData;
        packetData.block.messageId = messageId;
        packetData.block.fragmentId = fragmentId;
        packetData.block.fragmentSize = fragmentSize;
        packetData.block.numFragments = numFragments;
        packetData.block.messageType = messageType;

        const int messageTypeBits = bits_required( 0, m_messageFactory->GetNumTypes() - 1 );

        int fragmentBits = ConservativeFragmentHeaderEstimate + fragmentSize;

        if ( fragmentId == 0 )
        {
            MessageSendQueueEntry * entry = m_messageSendQueue->Find( packetData.block.messageId );

            yojimbo_assert( entry );
            yojimbo_assert( entry->message );

            packetData.block.message = (BlockMessage*) entry->message;

            m_messageFactory->AcquireMessage( packetData.block.message );

            fragmentBits += entry->measuredBits + messageTypeBits;
        }
        else
        {
            packetData.block.message = NULL;
        }

        return fragmentBits;
    }

    void ReliableOrderedChannel::AddFragmentPacketEntry( uint16_t messageId, uint16_t fragmentId, uint16_t sequence )
    {
        // todo
        //printf( "%p: add fragment packet entry: messageId = %d, fragmentId = %d, sequence = %d\n", this, messageId, fragmentId, sequence );
        SentPacketEntry * sentPacket = m_sentPackets->Insert( sequence );
        yojimbo_assert( sentPacket );
        if ( sentPacket )
        {
            sentPacket->numMessageIds = 0;
            sentPacket->messageIds = NULL;
            sentPacket->timeSent = m_time;
            sentPacket->acked = 0;
            sentPacket->block = 1;
            sentPacket->blockMessageId = messageId;
            sentPacket->blockFragmentId = fragmentId;
        }
    }

    void ReliableOrderedChannel::ProcessPacketFragment( int messageType, uint16_t messageId, int numFragments, uint16_t fragmentId, const uint8_t * fragmentData, int fragmentBytes, BlockMessage * blockMessage )
    {  
        yojimbo_assert( !m_config.disableBlocks );

        if ( fragmentData )
        {
            const uint16_t expectedMessageId = m_messageReceiveQueue->GetSequence();

            if ( messageId != expectedMessageId )
            {
                // todo
                //printf( "%p: wrong block message id: expected %d, got %d\n", this, expectedMessageId, messageId );
                return;
            }

            // start receiving a new block

            if ( !m_receiveBlock->active )
            {
                // todo
                // printf( "%p: start receiving new block: messageId = %d\n", this, messageId );

                yojimbo_assert( numFragments >= 0 );
                yojimbo_assert( numFragments <= m_config.GetMaxFragmentsPerBlock() );

                m_receiveBlock->active = true;
                m_receiveBlock->numFragments = numFragments;
                m_receiveBlock->numReceivedFragments = 0;
                m_receiveBlock->messageId = messageId;
                m_receiveBlock->blockSize = 0;
                m_receiveBlock->receivedFragment->Clear();
            }

            // validate fragment

            if ( fragmentId >= m_receiveBlock->numFragments )
            {
                // The fragment id is out of range.
                SetErrorLevel( CHANNEL_ERROR_DESYNC );
                return;
            }

            if ( numFragments != m_receiveBlock->numFragments )
            {
                // The number of fragments is out of range.
                SetErrorLevel( CHANNEL_ERROR_DESYNC );
                return;
            }

            // receive the fragment

            if ( !m_receiveBlock->receivedFragment->GetBit( fragmentId ) )
            {
                // todo
                // printf( "%p: received fragment: messageId = %d, fragmentId = %d\n", this, messageId, fragmentId );

                m_receiveBlock->receivedFragment->SetBit( fragmentId );

                memcpy( m_receiveBlock->blockData + fragmentId * m_config.fragmentSize, fragmentData, fragmentBytes );

                if ( fragmentId == 0 )
                {
                    m_receiveBlock->messageType = messageType;
                }

                if ( fragmentId == m_receiveBlock->numFragments - 1 )
                {
                    m_receiveBlock->blockSize = ( m_receiveBlock->numFragments - 1 ) * m_config.fragmentSize + fragmentBytes;

                    if ( m_receiveBlock->blockSize > (uint32_t) m_config.maxBlockSize )
                    {
                        // The block size is outside range
                        SetErrorLevel( CHANNEL_ERROR_DESYNC );
                        return;
                    }
                }

                m_receiveBlock->numReceivedFragments++;

                if ( fragmentId == 0 )
                {
                    // save block message (sent with fragment 0)

                    m_receiveBlock->blockMessage = blockMessage;

                    m_messageFactory->AcquireMessage( m_receiveBlock->blockMessage );
                }

                if ( m_receiveBlock->numReceivedFragments == m_receiveBlock->numFragments )
                {
                    // todo
                    // printf( "%p: finished receiving block: messageId = %d, fragmentId = %d\n", this, messageId, fragmentId );

                    // finished receiving block

                    if ( m_messageReceiveQueue->GetAtIndex( m_messageReceiveQueue->GetIndex( messageId ) ) )
                    {
                        // Did you forget to dequeue messages on the receiver?
                        SetErrorLevel( CHANNEL_ERROR_DESYNC );
                        return;
                    }

                    blockMessage = m_receiveBlock->blockMessage;

                    yojimbo_assert( blockMessage );

                    uint8_t * blockData = (uint8_t*) YOJIMBO_ALLOCATE( m_messageFactory->GetAllocator(), m_receiveBlock->blockSize );

                    if ( !blockData )
                    {
                        // Not enough memory to allocate block data
                        SetErrorLevel( CHANNEL_ERROR_OUT_OF_MEMORY );
                        return;
                    }

                    memcpy( blockData, m_receiveBlock->blockData, m_receiveBlock->blockSize );

                    blockMessage->AttachBlock( m_messageFactory->GetAllocator(), blockData, m_receiveBlock->blockSize );

                    blockMessage->SetId( messageId );

                    MessageReceiveQueueEntry * entry = m_messageReceiveQueue->Insert( messageId );
                    yojimbo_assert( entry );
                    entry->message = blockMessage;
                    m_receiveBlock->active = false;
                    m_receiveBlock->blockMessage = NULL;
                }
            }
        }
    }

    // ------------------------------------------------

    UnreliableUnorderedChannel::UnreliableUnorderedChannel( Allocator & allocator, MessageFactory & messageFactory, const ChannelConfig & config, int channelIndex, double time ) : Channel( allocator, messageFactory, config, channelIndex, time )
    {
        yojimbo_assert( config.type == CHANNEL_TYPE_UNRELIABLE_UNORDERED );

        m_messageSendQueue = YOJIMBO_NEW( *m_allocator, Queue<Message*>, *m_allocator, m_config.sendQueueSize );
        
        m_messageReceiveQueue = YOJIMBO_NEW( *m_allocator, Queue<Message*>, *m_allocator, m_config.receiveQueueSize );

        Reset();
    }

    UnreliableUnorderedChannel::~UnreliableUnorderedChannel()
    {
        Reset();

        YOJIMBO_DELETE( *m_allocator, Queue<Message*>, m_messageSendQueue );
        YOJIMBO_DELETE( *m_allocator, Queue<Message*>, m_messageReceiveQueue );
    }

    void UnreliableUnorderedChannel::Reset()
    {
        SetErrorLevel( CHANNEL_ERROR_NONE );

        for ( int i = 0; i < m_messageSendQueue->GetNumEntries(); ++i )
            m_messageFactory->ReleaseMessage( (*m_messageSendQueue)[i] );

        for ( int i = 0; i < m_messageReceiveQueue->GetNumEntries(); ++i )
            m_messageFactory->ReleaseMessage( (*m_messageReceiveQueue)[i] );

        m_messageSendQueue->Clear();
        m_messageReceiveQueue->Clear();
  
        ResetCounters();
    }

    bool UnreliableUnorderedChannel::CanSendMessage() const
    {
        yojimbo_assert( m_messageSendQueue );
        return !m_messageSendQueue->IsFull();
    }

    void UnreliableUnorderedChannel::SendMessage( Message * message )
    {
        yojimbo_assert( message );
        yojimbo_assert( CanSendMessage() );

        if ( GetErrorLevel() != CHANNEL_ERROR_NONE )
        {
            m_messageFactory->ReleaseMessage( message );
            return;
        }

        if ( !CanSendMessage() )
        {
            SetErrorLevel( CHANNEL_ERROR_SEND_QUEUE_FULL );
            m_messageFactory->ReleaseMessage( message );
            return;
        }

        yojimbo_assert( !( message->IsBlockMessage() && m_config.disableBlocks ) );

        if ( message->IsBlockMessage() && m_config.disableBlocks )
        {
            SetErrorLevel( CHANNEL_ERROR_BLOCKS_DISABLED );
            m_messageFactory->ReleaseMessage( message );
            return;
        }

        if ( message->IsBlockMessage() )
        {
            yojimbo_assert( ((BlockMessage*)message)->GetBlockSize() > 0 );
            yojimbo_assert( ((BlockMessage*)message)->GetBlockSize() <= m_config.maxBlockSize );
        }

        m_messageSendQueue->Push( message );

        m_counters[CHANNEL_COUNTER_MESSAGES_SENT]++;
    }

    Message * UnreliableUnorderedChannel::ReceiveMessage()
    {
        if ( GetErrorLevel() != CHANNEL_ERROR_NONE )
            return NULL;

        if ( m_messageReceiveQueue->IsEmpty() )
            return NULL;

        m_counters[CHANNEL_COUNTER_MESSAGES_RECEIVED]++;

        return m_messageReceiveQueue->Pop();
    }

    void UnreliableUnorderedChannel::AdvanceTime( double time )
    {
        (void) time;
    }
    
    int UnreliableUnorderedChannel::GetPacketData( ChannelPacketData & packetData, uint16_t packetSequence, int availableBits )
    {
        (void) packetSequence;

        if ( m_messageSendQueue->IsEmpty() )
            return 0;

        if ( m_config.packetBudget > 0 )
            availableBits = yojimbo_min( m_config.packetBudget * 8, availableBits );

        const int giveUpBits = 4 * 8;

        const int messageTypeBits = bits_required( 0, m_messageFactory->GetNumTypes() - 1 );

        int usedBits = ConservativeMessageHeaderEstimate;

        int numMessages = 0;

        Message ** messages = (Message**) alloca( sizeof( Message* ) * m_config.maxMessagesPerPacket );

        while ( true )
        {
            if ( m_messageSendQueue->IsEmpty() )
                break;

            if ( availableBits - usedBits < giveUpBits )
                break;

            if ( numMessages == m_config.maxMessagesPerPacket )
                break;

            Message * message = m_messageSendQueue->Pop();

            yojimbo_assert( message );

            MeasureStream measureStream( m_messageFactory->GetAllocator() );
            
            message->SerializeInternal( measureStream );
            
            if ( message->IsBlockMessage() )
            {
                BlockMessage * blockMessage = (BlockMessage*) message;
                SerializeMessageBlock( measureStream, *m_messageFactory, blockMessage, m_config.maxBlockSize );
            }

            const int messageBits = messageTypeBits + measureStream.GetBitsProcessed();
            
            if ( usedBits + messageBits > availableBits )
            {
                m_messageFactory->ReleaseMessage( message );
                continue;
            }

            usedBits += messageBits;
            
            yojimbo_assert( usedBits <= availableBits );
            
            messages[numMessages++] = message;
        }

        if ( numMessages == 0 )
            return 0;

        Allocator & allocator = m_messageFactory->GetAllocator();

        packetData.Initialize();
        packetData.channelIndex = GetChannelIndex();
        packetData.message.numMessages = numMessages;
        packetData.message.messages = (Message**) YOJIMBO_ALLOCATE( allocator, sizeof( Message* ) * numMessages );
        for ( int i = 0; i < numMessages; ++i )
        {
            packetData.message.messages[i] = messages[i];
        }

        return usedBits;
    }

    void UnreliableUnorderedChannel::ProcessPacketData( const ChannelPacketData & packetData, uint16_t packetSequence )
    {
        if ( m_errorLevel != CHANNEL_ERROR_NONE )
            return;
        
        if ( packetData.messageFailedToSerialize )
        {
            SetErrorLevel( CHANNEL_ERROR_FAILED_TO_SERIALIZE );
            return;
        }

        for ( int i = 0; i < (int) packetData.message.numMessages; ++i )
        {
            Message * message = packetData.message.messages[i];
            yojimbo_assert( message );  
            message->SetId( packetSequence );
            if ( !m_messageReceiveQueue->IsFull() )
            {
                m_messageFactory->AcquireMessage( message );
                m_messageReceiveQueue->Push( message );
            }
        }
    }

    void UnreliableUnorderedChannel::ProcessAck( uint16_t ack )
    {
        (void) ack;
    }
}
