#include <inspircd.h>
#include "account.h"

/* $ModDesc: Provides channel mode +r for channel registration */
/* $ModAuthor: webczat */
/* $ModDepends: core 2.1 */

/* class declarations */
class RegisterModeHandler;
/* database reader reads the channel database and returns structure with it */
/* structure for single entry */
struct EntryDescriptor
{
 std::string name, ts, topicset, topicsetby, topic, modes, registrant;
};
class DatabaseReader
{
 /* the entry descriptor */
  EntryDescriptor entry;
 /* file */
 FILE *fd;
 /* public entries */
 public:
 /* constructor, opens the file */
 DatabaseReader (std::string filename)
 {
  /* initialize */
  fd = NULL;
  /* open the file */
  fd = fopen (filename.c_str ( ), "r");
  /* if we can't open the file, return. */
  if (!fd) return;
 }
 /* destructor will close the file */
 ~DatabaseReader ( )
 {
  /* if fd is not null, close it */
  if (fd) fclose (fd);
 }
 /* get next entry */
 EntryDescriptor *next ( )
 {
  /* if fd is NULL, fake eof */
  if (!fd) return 0;
  /* clear data */
  entry.name = "";
  entry.ts = "";
  entry.registrant = "";
  entry.topicset = "";
  entry.topicsetby = "";
  entry.modes = "";
  entry.topic = "";
  std::string str;
  /* read single characters from the file and add them to the string, end at EOF or \n, ignore \r */
  while (1)
  {
   int c = fgetc (fd);
   if ((c == EOF) || ((unsigned char)c == '\n')) break;
   if ((unsigned char)c == '\r') continue;
   str.push_back ((unsigned char)c);
  }
  /* ready to parse the line */
  if (str == "") return 0;
  std::string token;
  irc::spacesepstream sep(str);
  /* get first one */
  bool res = sep.GetToken (token);
  /* malformed if it is not chaninfo */
  if (token != "chaninfo" || !res)
  {
   ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed channel database");
   return 0;
  }
  /* okay, read channel name */
  res = sep.GetToken (token);
  /* name was get, but if it's the last token, database is malformed */
  if (!res)
  {
   ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed channel database");
   return 0;
  }
  /* save name */
  entry.name = token;
  /* get next token, if it's the last one, db is malformed */
  res = sep.GetToken (token);
  if (!res)
  {
   ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed channel database");
   return 0;
  }
  /* set channel timestamp */
   entry.ts = token;
  /* get the last token */
  res = sep.GetToken (token);
  /* this time we ignore things after this token */
  /* set registrant */
  entry.registrant = token;
  /* initial entry read, read next lines in a loop until end, eof means malformed database again */
  while (1)
  {
   str.clear ( );
   /* read the line */
   while (1)
   {
    int c = fgetc (fd);
    if (c == EOF)
    {
     ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed channel database");
     return 0;
    }
     unsigned char c2 = (unsigned char)c;
    if (c2 == '\n') break;
    if (c2 == '\r') continue;
    str.push_back (c2);
   }
   irc::spacesepstream sep2(str);
   /* get the token */
   if (str == "")
   {
    ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed channel database");
    return 0;
   }
   res = sep2.GetToken (token);
   /* break the loop if token is "end" */
   if (token == "end") break;
   /* it is not, so the large if statement there */
   else if (token == "topic")
   {
    /* topic info, if end then it is malformed */
    if (sep2.StreamEnd ( ))
    {
     ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed topic declaration in channel database for channel %s", entry.name.c_str ( ));
    }
    /* if not, then read topic set time */
    res = sep2.GetToken (token);
    /* if end then malformed */
    if (!res)
    {
     ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed topic declaration in channel database for channel %s", entry.name.c_str ( ));
    }
    /* save that */
    entry.topicset = token;
    /* get next token */
    res = sep2.GetToken (token);
    /* if last then malformed */
    if (!res)
    {
     ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed topic declaration in channel database for channel %s", entry.name.c_str ( ));
    }
    entry.topicsetby = token;
    /* get rest of the declaration into the topic string, this is a topic */
    entry.topic = sep2.GetRemaining ( );
   } else if (token == "modes")
   {
    /* modes token, used for representing modes, it's the easier one, just load remaining data */
    if (sep2.StreamEnd ( ))
    {
     ServerInstance->Logs->Log ("MODULE", DEFAULT, "malformed modes declaration in channel database for channel %s", entry.name.c_str ( ));
     continue;
    }
    entry.modes = sep2.GetRemaining ( );
   }
  }
  /* return entry address */
  return &entry;
 }
};
/* class being a database writer, gets a database file name on construct */
class DatabaseWriter
{
 /* file stream */
 FILE *fd;
 std::string dbname, tmpname;
 ModeHandler *mh;
 /* public */
 public:
 /* constructor */
 DatabaseWriter (std::string filename, ModeHandler *handler)
 {
  fd = NULL;
  dbname = filename;
  tmpname = filename + ".tmp";
  mh = handler;
  /* ready, open temporary database */
  fd = fopen (tmpname.c_str ( ), "w");
  if (!fd)
  {
   ServerInstance->Logs->Log ("MODULE", DEFAULT, "cannot save to the channel database");
  }
 }
 /* destructor */
 ~DatabaseWriter ( )
 {
  if (fd)
  {
   /* saving has been ended, close the database and flush buffers */
   fclose (fd);
   /* rename the database file */
   if (rename (tmpname.c_str ( ), dbname.c_str ( )) == -1)
   {
    ServerInstance->Logs->Log ("MODULE", DEFAULT, "can't rename the database file");
   }
  }
 }
 /* save the single channel */
 void next (Channel *chan)
 {
  if (!fd) return;
  /* first, construct the chaninfo line */
  std::string line;
  line.append ("chaninfo ").append (chan->name).append (" ").append (ConvToStr (chan->age)).append (" ").append (chan->GetModeParameter (mh)).append 
("\n");
  if (fputs (line.c_str ( ), fd) == EOF)
  {
   ServerInstance->Logs->Log ("MODULE", DEFAULT, "unable to write the channel entry");
   fclose (fd);
   fd = NULL;
   return;
  }
  /* now, write the topic if present */
  if (!chan->topic.empty ( ))
  {
   line.clear ( );
   line.append ("topic ").append (ConvToStr (chan->topicset)).append (" ").append (chan->setby).append (" ").append (chan->topic).append ("\n");
   if (fputs (line.c_str ( ), fd) == EOF)
   {
    ServerInstance->Logs->Log ("MODULE", DEFAULT, "unable to write channel's topic declaration");
    fclose (fd);
    fd = NULL;
    return;
   }
  }
  /* get all channel modes */
  line.clear ( );
  irc::modestacker ms;
  chan->ChanModes (ms, MODELIST_FULL);
  line.append ("modes ").append (ms.popModeLine (FORMAT_PERSIST, INT_MAX, INT_MAX)).append ("\nend\n");
  if (fputs (line.c_str ( ), fd) == EOF)
  {
   ServerInstance->Logs->Log ("MODULE", DEFAULT, "can't write mode declaration");
   fclose (fd);
   fd = NULL;
   return;
  }
 }
};
/* class for handling +r mode */
class RegisterModeHandler : public ModeHandler
{
 /* this is the prefix required to use that mode */
 unsigned char prefixrequired;
 public:
 /* mode constructor */
 RegisterModeHandler (Module *me) : ModeHandler (me, "registered", 'r', PARAM_SETONLY, MODETYPE_CHANNEL)
 {
  /* set properties. */
  list = false;
  fixed_letter = false;
  oper = false;
  m_paramtype = TR_TEXT;
  /* warning, levelrequired is ignored in our case because this specific mode has specific permission checking that doesn't, for example, allow changing 
  permissions required to set it */
 }
 /* translate a parameter */
 void OnParameterMissing (User *user, User *undefined, Channel *chan, std::string &param)
 {
  /* we are called only if the registrant of a new channel didn't provide a parameter into the mode */
  /* if user is not logged in, we won't allow him and we'll send an error, if he is logged in then we'll use his account name */
  /* after account name is used, next permission checks will occur, but this function doesn't do it */
  /* get account extension item */
  AccountExtItem *it = GetAccountExtItem ( );
  std::string *acctname = it->get (user);
  if (!acctname || acctname->empty ( ))
  {
   /* user not logged in, so we don't have any account to use as a default one, just tell him that he can't do this, really. */
   user->WriteNumeric (ERR_CHANOPRIVSNEEDED, "%s You must be logged into an account to register a channel", chan->name.c_str ( ));
   return;
  }
  /* user is logged in, this doesn't mean we'll allow it, but we really deny it without a param, so set the parameter */
  param = *acctname;
 }
 /* make access checks */
 ModResult AccessCheck (User *source, Channel *chan, std::string &param, bool adding)
 {
  /* you have prefix configured, try to translate it into the prefix rank */
  unsigned int rankrequired = ServerInstance->Modes->FindPrefix (prefixrequired)->GetPrefixRank ( );
  /* and get the current rank of user issuing the command */
  unsigned int rank = chan->GetAccessRank (source);
  /* we have rank required to use the mode and rank of user trying to set it, let's check if rank of the user is greater or equal to the required one, 
  if it's not, deny */
  if (rank < rankrequired)
  {
   source->WriteNumeric (ERR_CHANOPRIVSNEEDED, "%s You must have prefix %c or above to register or unregister a channel", chan->name.c_str ( ), prefixrequired);
   return MOD_RES_DENY;
  }
  if (adding)
  {
   /* if you are adding +r mode to a channel, you register it, you have required permissions to register at all */
  /* now check if you have permissions to register using the given account */
   /* if you have the channels/set-registration oper privilege, then you can always set this mode */
   if (source->HasPrivPermission ("channels/set-registration", false)) return MOD_RES_ALLOW;
   /* if you don't have the privilege, you are a normal user or a ... mispermitted oper */
   /* get the current account user is logged into */
   std::string *acctname = GetAccountExtItem ( )->get (source);
   /* if the account name was not given, is empty or is not equal to the given parameter, deny */
   if (!acctname || acctname->empty ( ) || *acctname != param)
   {
    source->WriteNumeric (ERR_CHANOPRIVSNEEDED, "%s You must be logged into the account %s to register a channel", chan->name.c_str ( ), param.c_str ( ));
    return MOD_RES_DENY;
   }
  } else
  {
   /* yes, we are removing a mode, this time, even an ircop can't do it without samode unles he's a channel registrant */
   /* removal of +r is only allowed for user having at least required rank and also having registrant status */
   /* user has registrant status if +r mode set on a channel he's on has it's parameter set to the account user's currently logged in */
   /* if the mode +r is not set, don't check permissions */
   if (!chan->IsModeSet (this)) return MOD_RES_PASSTHRU;
   /* get account name of the user */
   std::string *acctname = GetAccountExtItem ( )->get (source);
   /* check if user was really logged in */
   if (!acctname || acctname->empty ( ))
   {
    /* he is not logged in */
    source->WriteNumeric (ERR_CHANOPRIVSNEEDED, "%s You must be logged into an account to unregister a channel", chan->name.c_str ( ));
    /* deny */
    return MOD_RES_DENY;
   }
   /* user is logged in, +r mode is set with a parameter, try to get the parameter */
   std::string registrantname = chan->GetModeParameter (this);
   /* due to checking of the mode while we were adding it, we are sure that parameter is valid */
   /* if accountname is not the same as registrantname, deny access */
   if (*acctname != registrantname)
   {
    source->WriteNumeric (ERR_CHANOPRIVSNEEDED, "%s Only the person who registered a channel may unregister it", chan->name.c_str ( ));
    return MOD_RES_DENY;
   }
  }
  /* okay, we did all permission checking to ensure that anything is okay and the user source has all required permissions to register or unregister the 
channel */
  /* allow it */
  return MOD_RES_ALLOW;
 }
 /*what to do on mode change? */
 ModeAction OnModeChange (User *source, User *undefined, Channel *chan, std::string &param, bool adding)
 {
  /* the modechange itself is performed */
  /* if we are adding the mode, it means we are registering the channel */
  if (adding)
  {
   /* we won't do anything if mode is set */
   if (chan->IsModeSet (this)) return MODEACTION_DENY;
   /* we are doing that only if we aren't fakeclient */
   if (source != ServerInstance->FakeClient)
   {
    /* first, send a message to all ircops with snomask +r set */
    ServerInstance->SNO->WriteGlobalSno ('r', "%s registered channel %s to account %s", source->GetFullHost ( ).c_str ( ), chan->name.c_str ( ), param.c_str ( ));
    /* now, send similar to channel */
    chan->WriteChannel (ServerInstance->FakeClient, "NOTICE %s :This channel has been registered", chan->name.c_str ( ));
   }
   /* set channel registrant by setting parameter of mode */
   chan->SetModeParam (this, param);
   /* set the mode itself */
   chan->SetMode (this, true);
   /* and allow further processing */
  } else
  {
   /* if not, we are unregistering it */
   /*if the channel mode is unset, don't set it again and deny */
   if (!chan->IsModeSet (this)) return MODEACTION_DENY;
   if (source != ServerInstance->FakeClient)
   {
    /* it is set, so first send a server notice to all ircops using +r snomask that this channel has been unregistered */
    ServerInstance->SNO->WriteGlobalSno ('r', "channel %s unregistered by %s", chan->name.c_str ( ), source->GetFullHost ( ).c_str ( ));
    /* then send this important message to the channel as the notice */
    chan->WriteChannel (ServerInstance->FakeClient, "NOTICE %s :This channel has been unregistered", chan->name.c_str ( ));
   }
   /* then unset the mode */
   chan->SetMode (this, false);
   /* now, if usercount is 0, delete the channel */
   if (!chan->GetUserCounter ( )) chan->DelUser (ServerInstance->FakeClient);
   /* and allow modechange to proceed */
  }
  /* allow further actions */
  return MODEACTION_ALLOW;
 }
 /* set a prefix */
 void set_prefixrequired (unsigned char c)
 {
  prefixrequired = c;
 }
};

/* module class */
class ChannelRegistrationModule : public Module
{
 private:
 /* some config variables */
 std::string chandb;
 bool dirty;
 /* modehandler handling registration mode */
 RegisterModeHandler *mh;
 /* called by the module only */
 /* write the channel database */
 void WriteDatabase ( )
 {
  /* step1: create the database writer that opens the temporary database file for writing */
  DatabaseWriter db (chandb, mh);
  /* ready, start a loop */
  for (chan_hash::const_iterator i = ServerInstance->chanlist->begin ( ); i != ServerInstance->chanlist->end ( ); i++)
  {
   /* write the channel if it has mode set */
   if (i->second->IsModeSet (mh)) db.next (i->second);
  }
  /* completed all iterations, function now goes down, that will destruct the database writer */
  /* while destruction, it will close the temporary file and rename it to a real database file, completing write process */
 }
 /* read the channel database */
 void ReadDatabase ( )
 {
  /* create the reader object and open the database */
  DatabaseReader db (chandb);
  /* start the database read loop */
  EntryDescriptor *entry;
  while ((entry = db.next ( )))
  {
   /* we get a database entry */
   /* so if channel name doesn't start with #, give an error and continue */
   if (entry->name[0] != '#')
   {
    ServerInstance->Logs->Log ("MODULE", DEFAULT, "invalid channel name in channel database");
    continue;
   }
   /* entry is valid */
   /* try to find the channel */
   Channel *chan = ServerInstance->FindChan (entry->name);
   /* now, things that will be done depends on if channel was found or not */
   if (chan)
   {
    /* if it was found, then channel timestamp becomes not important, only things needing to be set is the registrant status, do it */
    /* create the mode stacker */
    irc::modestacker ms;
    /* we need to push the mode change */
    ms.push (irc::modechange ("registered", entry->registrant));
    /* make the server process and apply the mode change */
    ServerInstance->Modes->Process (ServerInstance->FakeClient, chan, ms);
    /* we don't do it below because channel doesn't exist then */
    /* but in this situation, channel exists and there are users on it, they must know that mode has been changed */
    /* send that info to them */
    ServerInstance->Modes->Send (ServerInstance->FakeClient, chan, ms);
   } else
   {
    /* channel does not exist, allocate it, it will be constructed and added to the network as an empty channel */
    chan = new Channel (entry->name, atol (entry->ts.c_str ( )));
    /* empty modeless channel added and allocated */
    /* so, channel exists in the network, then if topic is not empty, set data */
    if (!entry->topic.empty ( ))
    {
     chan->topic = entry->topic;
     chan->topicset = atol (entry->topicset.c_str ( ));
     chan->setby = entry->topicsetby;
    }
    /* if modestring is not empty, then set all modes in it */
    if (!entry->modes.empty ( ))
    {
     /* create sepstream */
     irc::spacesepstream sep(entry->modes);
     /* create modestacker for stacking all mode changes */
     irc::modestacker ms;
     /* spacesepstream is used to iterate through all modes that were saved for a channel */
     /* modestacker ms is used to stack them */
     /* iterate through the mode list */
     while (!sep.StreamEnd ( ))
     {
      /* get the mode to be applied */
      std::string token, name, value;
      sep.GetToken (token);
      /* the mode looks like name=value unless it doesn't have any value then it's just name */
      /* find the position of = sign */
      size_t namepos = token.find ('=');
      /* if we didn't find anything, set name to the token because mode is... valueless */
      if (namepos == std::string::npos) name = token;
      /* if it found that = sign, we have both name and value */
      else
      {
       /* it found it, name is string before the character, value is the string after it */
       name = token.substr (0, namepos);
       value = token.substr (namepos + 1);
      }
      /* mode name and value found, we can now add it to the modestacker */
      /* hmm, or we can't, reason is that letters are separate in user and channel modes, but mode names are not, so what if this thing we're setting is 
      an user mode? */
      /* to check that, we must find the mode */
      ModeHandler *mc = ServerInstance->Modes->FindMode (name);
      /* those actions will be taken only if mode was found and if it's the channel mode */
      if (mc && mc->GetModeType ( ) == MODETYPE_CHANNEL)
      {
       /* mode was found and is not an user mode but a channel mode, push it to the modestacker, we use id instead of name for quicker push because we 
have the mode handler now */
       ms.push (irc::modechange (mc->id, value));
      }
     }
     /* okay, all modes were pushed, modestacker can now produce the mode line */
     /* so, process all mode changes and apply them on a channel */
     ServerInstance->Modes->Process (ServerInstance->FakeClient, chan, ms);
    }
    /* if some error was found like no modes line, set registrant */
    if (!chan->IsModeSet (mh))
    {
     irc::modestacker ms2;
     ms2.push (irc::modechange ("registered", entry->registrant));
     ServerInstance->Modes->Process (ServerInstance->FakeClient, chan, ms2);
    }
   }
  }
 }
 public:
 /* get module version and flags. */
 Version GetVersion ( )
 {
  return Version("Provides channel mode +r for channel registration", VF_COMMON);
 }
 /* initialize a module */
 void init ( )
 {
  /* enable the snomask for channel registration */
  ServerInstance->SNO->EnableSnomask ('r', "chanregister");
  /* attach events */
  Implementation eventlist[] = {I_OnRehash, I_OnCheckJoin, I_OnPermissionCheck, I_OnChannelPreDelete, I_OnBackgroundTimer, I_OnMode, 
I_OnPostTopicChange, I_OnRawMode};
  ServerInstance->Modules->Attach (eventlist, this, 8);
  /* allocate the mode handler */
  mh = new RegisterModeHandler(this);
  /* add a new service that is a new channel mode handler for handling channel registration mode */
  ServerInstance->Modules->AddService (*mh);
  /* call OnRehash event */
  OnRehash (NULL);
  /* rehash procedures finished, read the database */
  ReadDatabase ( );
  /* after database read, it's possible some channels must be updated, do it now */
  WriteDatabase ( );
 }
 /* module cleanup, called before unload */
 CullResult cull ( )
 {
  /* just delete modehandler from the system and deallocate it */
  delete mh;
  return Module::cull ( );
 }
 /* rehash event */
 void OnRehash (User *u)
 {
  /* try to download the tag from the config system */
  ConfigTag *chregistertag = ServerInstance->Config->ConfValue ("chanregister");
  /* get the prefix character */
  unsigned char prefixchar = chregistertag->getString ("prefix", "@")[0];
  /* get the channel database */
  chandb = chregistertag->getString ("chandb", "data/channels.db");
  /* check if prefix exists, if not, throw an exception */
  if (!ServerInstance->Modes->FindPrefix (prefixchar)) throw CoreException ("Module providing the configured prefix is not loaded");
  mh->set_prefixrequired (prefixchar);
 }
 /* OnCheckJoin - this is an event for checking permissions of some user to join some channel, it is used to allow joining by registrants even when 
banned */
 void OnCheckJoin (ChannelPermissionData &joindata)
 {
  /* if channel is null, it's not something that is done on channels, or it is but channel is being created, so no registrant in it */
  if (!joindata.chan) return;
  /* channel is okay, permission name is join, now, check if user is logged in, if not then return */
  std::string *acctname = GetAccountExtItem ( )->get (joindata.source);
  /* if account is not found or empty, we can be really sure that we really aren't registrant of any channel */
  if (!acctname || acctname->empty ( )) return;
  /* return if mode is not set */
  if (!joindata.chan->IsModeSet (mh)) return;
  /* it is set and we have parameter, get it */
  std::string registrantname = joindata.chan->GetModeParameter (mh);
  /* if registrantname and account name are the same, override */
  if (registrantname == *acctname) joindata.result = MOD_RES_ALLOW;
 }
 /* check permissions */
 void OnPermissionCheck (PermissionData &perm)
 {
  /* if channel is null, proceed normally */
  if (!perm.chan) return;
  /* if not, but name of permission is join, proceed normally because it was checked */
  if (perm.name == "join") return;
  /* if +r mode is changed, return */
  if (perm.name == "mode/registered") return;
  /* if anything is ok, but mode +r is unset, return and check perms normally */
  if (!perm.chan->IsModeSet (mh)) return;
  /* if set, get the registrant account name */
  std::string registrantname = perm.chan->GetModeParameter (mh);
  /* get account name of the current user */
  std::string *acctname = GetAccountExtItem ( )->get (perm.source);
  /* if user is not logged in then return */
  if (!acctname || acctname->empty ( )) return;
  /* if ok, then set result to allow if registrant name matches account name */
  if (*acctname == registrantname) perm.result = MOD_RES_ALLOW;
 }
 /* called before channel is being deleted */
 ModResult OnChannelPreDelete (Channel *chan)
 {
  /* return 1 to prevent channel deletion if channel is registered */
  if (chan->IsModeSet (mh)) return MOD_RES_DENY;
  /* in other case, return 0, to allow channel deletion */
  return MOD_RES_PASSTHRU;
 }
 /* called on background timer to write all channels to disk if they were changed */
 void OnBackgroundTimer (time_t cur)
 {
  /* if not dirty then don't do anything */
  if (!dirty) return;
  /* dirty, one of registered channels was changed, save it */
  WriteDatabase ( );
  /* clear dirty to prevent next savings */
  dirty = false;
 }
 /* after the topic has been changed, call this to check if channel was registered */
 void OnPostTopicChange (User *user, Channel *chan, const std::string &topic)
 {
  /* if the channel is not registered, don't do anything, but if channel has been registered, this must be saved, so mark database as to be saved */
  if (chan->IsModeSet (mh)) dirty = true;
 }
 /* this event is called after each modechange */
 void OnMode (User *user, Extensible *target, const irc::modestacker &modes)
 {
  /* dynamic-cast the extensible to a channel, it may not work */
  Channel *chan = dynamic_cast<Channel *> (target);
  /* if null then it's not a channel so return */
  if (!chan) return;
  /* if it is, and it's a registered channel, then the database was changed and must be saved */
  if (chan->IsModeSet (mh)) dirty = true;
 }
 /* when someone unsets +r, OnMode event won't mark the database as requiring saving, but it requires to be saved, and because of this, this event 
 handler is needed */
 /* it catches all mode changes, and sets database as dirty if register mode is being removed */
 ModResult OnRawMode (User *user, Channel *chan, irc::modechange &mc)
 {
  /* if mode is being removed and this is our mode then mark as dirty */
  if (!mc.adding && mc.mode == mh->id) dirty = true;
  /* this is the only thing this module does, so pass through */
  return MOD_RES_PASSTHRU;
 }
};
/* register the module */
MODULE_INIT(ChannelRegistrationModule)