//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

#include "networkirc.hpp"

using namespace Huggle::IRC;

NetworkIrc::NetworkIrc(QString server, QString nick)
{
    this->Ident = "huggle";
    this->Nick = nick;
    this->Port = 6667;
    this->Server = server;
    this->UserName = "Huggle client";
    this->Timer = new QTimer(this);
    this->NetworkSocket = NULL;
    this->NetworkThread = NULL;
    this->MessagesLock = new QMutex(QMutex::Recursive);
    this->ChannelsLock = new QMutex(QMutex::Recursive);
}

NetworkIrc::~NetworkIrc()
{
    this->ChannelsLock->lock();
    // we need to delete all instances of channels before we wipe the hash table
    QStringList keys = this->Channels.keys();
    while (keys.count() > 0)
    {
        delete this->Channels[keys.at(0)];
        this->Channels.remove(keys.at(0));
        keys.removeAt(0);
    }
    this->ChannelsLock->unlock();
    delete this->MessagesLock;
    delete this->NetworkSocket;
    delete this->ChannelsLock;
    delete this->Timer;
    delete this->NetworkThread;
}

bool NetworkIrc::Connect()
{
    if (this->NetworkThread != NULL)
    {
        if (this->NetworkThread->__IsConnecting)
        {
            throw new Huggle::Exception("You attempted to connect NetworkIrc which is already connecting", "bool NetworkIrc::Connect()");
        }
        if (this->NetworkThread->__Connected)
        {
            throw new Huggle::Exception("You attempted to connect NetworkIrc which is already connected");
        }
    }
    if (this->NetworkThread != NULL)
    {
        delete this->NetworkThread;
    }
    this->NetworkThread = new NetworkIrc_th();
    this->NetworkThread->root = this;
    if (this->NetworkSocket != NULL)
    {
        delete this->NetworkSocket;
    }
    this->NetworkSocket = new QTcpSocket();
    connect(this->NetworkSocket, SIGNAL(readyRead()), this, SLOT(OnReceive()));
    this->NetworkThread->__IsConnecting = true;
    this->NetworkSocket->connectToHost(this->Server, this->Port);
    if (!this->NetworkSocket->waitForConnected())
    {
        this->NetworkThread->__IsConnecting = false;
        return false;
    }
    this->Data("USER " + this->Ident + " 8 * :" + this->UserName);
    QString nick = this->Nick;
    nick = nick.replace(" ", "");
    this->Data("NICK " + nick);
    this->NetworkThread->start();
    connect(this->Timer, SIGNAL(timeout()), this, SLOT(OnTime()));
    this->Timer->start(100);
    return true;
}

bool NetworkIrc::IsConnected()
{
    if (this->NetworkThread == NULL)
    {
        return false;
    }
    return this->NetworkThread->__Connected;
}

bool NetworkIrc::IsConnecting()
{
    if (this->NetworkThread == NULL)
    {
        return false;
    }
    return this->NetworkThread->__IsConnecting;
}

void NetworkIrc::Disconnect()
{
    if (!this->IsConnected() && !this->IsConnecting())
    {
        return;
    }
    this->Data("QUIT :Huggle (" + Huggle::Configuration::HuggleConfiguration->HuggleVersion +
               "), the anti vandalism software. See #huggle on irc://chat.freenode.net");
    this->NetworkSocket->disconnect();
    this->Timer->stop();
    this->NetworkThread->__IsConnecting = false;
    this->NetworkThread->__Connected = false;
    if (this->NetworkThread != NULL)
    {
        // we have to request the network thread to stop
        this->NetworkThread->Running = false;
    }
}

void NetworkIrc::Join(QString name)
{
    this->Data("JOIN " + name);
}

void NetworkIrc::Part(QString name)
{
    this->Data("PART " + name);
}

void NetworkIrc::Data(QString text)
{
    if (this->NetworkThread == NULL || this->NetworkSocket == NULL)
    {
        throw new Exception("You can't send data to network which you never connected to", "void NetworkIrc::Data(QString text)");
    }
    this->NetworkSocket->write((text + "\n").toUtf8());
}

void NetworkIrc::Send(QString name, QString text)
{
    this->Data("PRIVMSG " + name + " :" + text);
}

Message* NetworkIrc::GetMessage()
{
    Message *message;
    this->MessagesLock->lock();
    if (this->Messages.count() == 0)
    {
        this->MessagesLock->unlock();
        return NULL;
    } else
    {
        message = new Message(Messages.at(0));
        this->Messages.removeAt(0);
    }
    this->MessagesLock->unlock();
    return message;
}

void NetworkIrc::OnReceive()
{
    QString data(this->NetworkSocket->readLine());
    this->NetworkThread->lIOBuffers->lock();
    while (data != "")
    {
        this->NetworkThread->IncomingBuffer.append(data);
        data = QString(this->NetworkSocket->readLine());
    }
    this->NetworkThread->lIOBuffers->unlock();
}

void NetworkIrc::OnTime()
{
    this->NetworkThread->lIOBuffers->lock();
    while (this->NetworkThread->OutgoingBuffer.count() > 0)
    {
        this->Data(this->NetworkThread->OutgoingBuffer.at(0));
        this->NetworkThread->OutgoingBuffer.removeAt(0);
    }
    this->NetworkThread->lIOBuffers->unlock();
}

Message::Message(QString text, User us)
{
    this->Text = text;
    this->Channel = "";
    this->user = us;
}

Message::Message(const Message &ms)
{
    this->Text = ms.Text;
    this->Channel = ms.Channel;
    this->user = ms.user;
}

Message::Message(QString chan, QString text, User us)
{
    this->Channel = chan;
    this->Text = text;
    this->user = us;
}

Message::Message()
{
    this->Channel = "";
    this->Text = "";
}

Message::Message(Message *ms)
{
    this->Channel = ms->Channel;
    this->Text = ms->Text;
    this->user = ms->user;
}

User::User(QString nick, QString ident, QString host)
{
    this->Nick = nick;
    this->Ident = ident;
    this->Host = host;
}

User::User()
{
    this->Ident = "";
    this->Nick = "";
    this->Host = "";
}

User::User(User *user)
{
    this->Host = user->Host;
    this->Ident = user->Ident;
    this->Nick = user->Nick;
}

User::User(const User &user)
{
    this->Host = user.Host;
    this->Ident = user.Ident;
    this->Nick = user.Nick;
}

NetworkIrc_th::NetworkIrc_th()
{
    this->s = NULL;
    this->__Connected = false;
    this->__IsConnecting = false;
    this->lIOBuffers = new QMutex(QMutex::Recursive);
    this->root = NULL;
    this->Running = true;
}

NetworkIrc_th::~NetworkIrc_th()
{
    delete this->s;
    delete this->lIOBuffers;
}

void NetworkIrc_th::Data(QString text)
{
    this->lIOBuffers->lock();
    this->OutgoingBuffer.append(text);
    this->lIOBuffers->unlock();
}

void NetworkIrc_th::Line(QString line)
{
    QString Command = "";
    QString Source_ = "";
    if (line.startsWith("PING :"))
    {
        QString text = line.mid(6);
        this->Data("PONG :" + text);
    }
    if (!line.startsWith(":") || !line.contains(" "))
    {
        return;
    }

    line.replace("\r\n", "");
    QString Parameters_ = line;
    QString Message_ = "";
    if (Parameters_.contains(" :"))
    {
        // we store the index so that we don't need to look it up twice
        int index_ = Parameters_.indexOf(" :");
        Message_ = Parameters_.mid(index_ + 2);
        Parameters_ = Parameters_.mid(0, index_);
    }
    Parameters_ = Parameters_.mid(1);
    Source_ = Parameters_.mid(0, Parameters_.indexOf(" "));
    Parameters_= Parameters_.mid(Parameters_.indexOf(" ") + 1);
    if (!Parameters_.contains(" "))
    {
        Command = Parameters_;
        Parameters_ = "";
    } else
    {
        Command = Parameters_.mid(0, Parameters_.indexOf(" "));
        Parameters_ = Parameters_.mid(Parameters_.indexOf(" ") + 1);
    }

    if (Command == "002")
    {
        this->__IsConnecting = false;
        this->__Connected = true;
        return;
    }
    /// \todo implement TOPIC
    /// \todo implement CTCP
    /// \todo implement NOTICES
    if (Command == "PRIVMSG")
    {
        this->ProcessPrivmsg(Source_, Parameters_, Message_);
        return;
    }

    if (Command == "JOIN")
    {
        if (Parameters_ == "" && Message_ == "")
        {
            Syslog::HuggleLogs->DebugLog("Invalid channel name: " + line);
            return;
        }
        this->ProcessJoin(Source_, Parameters_, Message_);
        return;
    }

    if (Command == "322")
    {
        this->ProcessChannel(Source_, Parameters_);
        return;
    }

    if (Command == "KICK")
    {
        this->ProcessKick(Source_, Parameters_, Message_);
        return;
    }

    if (Command == "QUIT")
    {
        this->ProcessQuit(Source_, Message_);
        return;
    }

    if (Command == "PART")
    {
        this->ProcessPart(Source_, Parameters_, Message_);
        return;
    }
}

void NetworkIrc_th::ProcessPrivmsg(QString source, QString parameters, QString message)
{
    User user;
    user.Nick = source.mid(0, source.indexOf("!"));
    Message Message_;
    if (!parameters.contains("#"))
    {
        return;
    }
    Message_.Channel = parameters;
    Message_.user = user;
    Message_.Text = message;
    this->root->MessagesLock->lock();
    this->root->Messages.append(Message_);
    this->root->MessagesLock->unlock();
}

void NetworkIrc_th::ProcessJoin(QString source, QString channel, QString message)
{
    User user;
    user.Nick = source.mid(0, source.indexOf("!"));
    if (channel == "")
    {
        // some irc servers are providing channel name as a message and not
        // parameter, this is case of wikimedia irc server
        channel = message;
    }
    if (channel == "")
    {
        throw new Huggle::Exception("Invalid channel name", "void NetworkIrc_th::ProcessJoin"\
                                    "(QString source, QString channel, QString message)");
    }
    channel = channel.toLower();
    // first lock the channel list and check if we know this channel
    // it is also possible that this is us joining the channel
    // and not some other user so in this case we need to
    // make a new instance for this channel and put ourselve in
    this->root->ChannelsLock->lock();
    if (this->root->Channels.contains(channel))
    {
        // this is a known channel to us
        Channel *channel_ptr_ = this->root->Channels[channel];
        channel_ptr_->InsertUser(user);
    } else
    {
        // check if it's us who joined the channel
        if (this->root->Nick.toLower() == user.Nick.toLower())
        {
            Channel *channel_ptr_ = new Channel(channel);
            this->root->Channels.insert(channel, channel_ptr_);
            channel_ptr_->InsertUser(user);
        } else
        {
            Syslog::HuggleLogs->DebugLog("Ignoring JOIN event for unknown channel, user " + user.Nick + " channel " + channel);
        }
    }
    this->root->ChannelsLock->unlock();
}

void NetworkIrc_th::ProcessChannel(QString channel, QString data)
{

}

void NetworkIrc_th::ProcessKick(QString source, QString parameters, QString message)
{

}

void NetworkIrc_th::ProcessQuit(QString source, QString message)
{

}

void NetworkIrc_th::ProcessPart(QString source, QString channel, QString message)
{

}

void NetworkIrc_th::run()
{
    int ping = 0;
    while (this->Running)
    {
        this->lIOBuffers->lock();
        QStringList buffer;
        if (this->IncomingBuffer.count() > 0)
        {
            buffer += this->IncomingBuffer;
            this->IncomingBuffer.clear();
        }
        this->lIOBuffers->unlock();
        while (buffer.count() > 0)
        {
            QString data = buffer.at(0);
            buffer.removeAt(0);
            this->Line(data);
            Syslog::HuggleLogs->DebugLog("Processing IRC input from " + this->root->Server + ": " + data, 10);
        }
        ping++;
        if (ping > 2000)
        {
            this->Data("PING :" + this->root->Server);
            ping = 0;
        }
        this->usleep(10000);
    }
    this->__Connected = false;
    return;
}

Channel::Channel(QString name)
{
    this->Name = name;
    this->UsersChange_ = false;
    this->UsersLock = new QMutex(QMutex::Recursive);
}

Channel::~Channel()
{
    this->UsersLock->lock();
    this->Users.clear();
    this->UsersLock->unlock();
    delete this->UsersLock;
}

void Channel::InsertUser(User user)
{
    // we need to keep the user nicks in lower case in hash so that we can't have multiple same nicks
    // with different letter case which isn't possible on irc
    QString nick = user.Nick.toLower();
    this->UsersLock->lock();
    if (this->Users.contains(nick))
    {
        // there already is this user in a list, so we just update it
        User *user_ptr_ = &this->Users[nick];
        user_ptr_->Nick = user.Nick;
        user_ptr_->Ident = user.Ident;
        user_ptr_->Host = user.Host;
    } else
    {
        this->Users.insert(nick, user);
    }
    this->UsersLock->unlock();
    this->UsersChange_ = true;
}

bool Channel::UsersChanged()
{
    if (this->UsersChange_)
    {
        this->UsersChange_ = false;
        return true;
    }
    return false;
}
