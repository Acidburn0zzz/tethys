/* Tethys, c_user.c -- user commands
   Copyright (C) 2013 Alex Iadicicco

   This file is protected under the terms contained
   in the COPYING file in the project root */

#include "ircd.h"

static int m_echo(u_conn *conn, u_msg *msg)
{
	u_user *u = conn->priv;
	char buf[512];
	int i;

	snf(FMT_USER, buf, 512, ":%S NOTICE %U :***", &me, u);

	u_conn_f(conn, "%s Source: %E", buf, msg->src);
	u_conn_f(conn, "%s Command: %s", buf, msg->command);
	u_conn_f(conn, "%s Recieved %d arguments:", buf, msg->argc);

	for (i=0; i<msg->argc; i++)
		u_conn_f(conn, "%s %3d. ^%s$", buf, i, msg->argv[i]);
	return 0;
}

static int m_names(u_conn *conn, u_msg *msg)
{
	u_user *u = conn->priv;
	u_chan *c;

	/* TODO: no arguments version */
	if (msg->argc == 0)
		return 0;

	if (!(c = u_chan_get(msg->argv[0])))
		return u_user_num(u, ERR_NOSUCHCHANNEL, msg->argv[0]);

	u_chan_send_names(c, u);
	return 0;
}

struct m_whois_cb_priv {
	u_user *u, *tu;
	char *s, buf[512];
	uint w;
};

static void m_whois_cb(u_map *map, u_chan *c, u_chanuser *cu, struct m_whois_cb_priv *priv)
{
	char *p, buf[MAXCHANNAME+3];
	int retrying = 0;

	if ((c->mode & (CMODE_PRIVATE | CMODE_SECRET))
	    && !u_chan_user_find(c, priv->u))
		return;

	p = buf;
	if (cu->flags & CU_PFX_OP)
		*p++ = '@';
	if (cu->flags & CU_PFX_VOICE)
		*p++ = '+';
	strcpy(p, c->name);

try_again:
	if (!wrap(priv->buf, &priv->s, priv->w, buf)) {
		if (retrying) {
			u_log(LG_SEVERE, "Can't fit %s into %s!",
			      buf, "RPL_WHOISCHANNELS");
			return;
		}
		u_user_num(priv->u, RPL_WHOISCHANNELS,
			   priv->tu->nick, priv->buf);
		retrying = 1;
		goto try_again;
	}
}

static int m_whois(u_conn *conn, u_msg *msg)
{
	u_user *tu, *u = conn->priv;
	u_server *serv;
	char *nick;
	struct m_whois_cb_priv cb_priv;

	/*
	WHOIS aji aji
	:host.irc 311 x aji alex ponychat.net * :Alex Iadicicco
	:host.irc 319 x aji :#chan #foo ...
	*        ***** *   **   = 9
	:host.irc 312 x aji some.host :Host Description
	:host.irc 313 x aji :is a Server Administrator
	:host.irc 671 x aji :is using a secure connection
	:host.irc 317 x aji 1961 1365205045 :seconds idle, signon time
	:host.irc 330 x aji aji :is logged in as
	:host.irc 318 x aji :End of /WHOIS list.
	*/

	nick = strchr(msg->argv[0], ',');
	if (nick != NULL)
		*nick = '\0';
	nick = msg->argv[0];

	if (!(tu = u_user_by_nick(nick)))
		return u_user_num(u, ERR_NOSUCHNICK, nick);

	serv = u_user_server(tu);

	u_user_num(u, RPL_WHOISUSER, tu->nick, tu->ident, tu->host, tu->gecos);

	cb_priv.u = u;
	cb_priv.tu = tu;
	cb_priv.s = cb_priv.buf;
	cb_priv.w = 512 - (strlen(me.name) + strlen(u->nick) + strlen(tu->nick) + 9);
	u_map_each(tu->channels, (u_map_cb_t*)m_whois_cb, &cb_priv);
	if (cb_priv.s != cb_priv.buf) /* left over */
		u_user_num(u, RPL_WHOISCHANNELS, tu->nick, cb_priv.buf);

	u_user_num(u, RPL_WHOISSERVER, tu->nick, serv->name, serv->desc);

	if (tu->away[0])
		u_user_num(u, RPL_AWAY, tu->nick, tu->away);

	if (tu->flags & UMODE_OPER)
		u_user_num(u, RPL_WHOISOPERATOR, tu->nick);

	u_user_num(u, RPL_ENDOFWHOIS, tu->nick);
	return 0;
}

static int m_userhost(u_conn *conn, u_msg *msg)
{
	/* USERHOST user1 user2... usern 
	 * :host.irc 302 nick :user1=+~user@host user2=+~user@host ...
	 * *        *****    **   = 8
	 */
	u_user *tu, *u = conn->priv;
	int i, w, max;
	char buf[512], data[512];
	char *ptr = buf;

	max = 501 - strlen(me.name) - strlen(u->nick);
	buf[0] = '\0';

	/* TODO - last param could contain multiple targets */
	for (i=0; i<msg->argc; i++) {
		tu = u_user_by_nick(msg->argv[i]);
		if (tu == NULL)
			continue;

		w = snf(FMT_USER, data, 512, "%s%s=%c%s@%s", tu->nick,
		            ((tu->flags & UMODE_OPER) ? "*" : ""),
		            (tu->away[0] ? '-' : '+'),
		            tu->ident, tu->host);

		if (ptr + w + 1 > buf + max)
			u_user_num(u, RPL_USERHOST, buf);
			ptr = buf;

		if (ptr != buf)
			*ptr++ = ' ';

		u_strlcpy(ptr, data, buf + max - ptr);
		ptr += w;
	}

	if (ptr != buf)
		u_user_num(u, RPL_USERHOST, buf);
	return 0;
}

/* :serv.irc 352 aji #chan ident my.host serv.irc nick H*@ :hops realname */
static void who_reply(u_user *u, u_user *tu, u_chan *c, u_chanuser *cu)
{
	u_server *serv;
	char *s, buf[6];
	s = buf;

	if (c != NULL && cu == NULL)
		cu = u_chan_user_find(c, u);
	if (cu == NULL) /* this is an error */
		c = NULL;

	serv = u_user_server(tu);

	*s++ = tu->away[0] ? 'G' : 'H';
	if (tu->flags & UMODE_OPER)
		*s++ = '*';
	if (cu != NULL && (cu->flags & CU_PFX_OP))
		*s++ = '@';
	if (cu != NULL && (cu->flags & CU_PFX_VOICE))
		*s++ = '+';
	*s++ = '\0';

	u_user_num(u, RPL_WHOREPLY, c, tu->ident, tu->host,
	           serv->name, tu->nick, buf, 0, tu->gecos);
}

static void m_who_chan_cb(u_map *map, u_user *tu, u_chanuser *cu, u_user *u)
{
	who_reply(u, tu, cu->c, cu);
}

static int m_who(u_conn *conn, u_msg *msg)
{
	u_user *tu, *u = conn->priv;
	u_chan *c = NULL;
	char *name = msg->argv[0];

	/* TODO: WHOX, operspy? */

	if (strchr(CHANTYPES, name[0])) {
		if ((c = u_chan_get(name)) == NULL)
			goto end;

		u_map_each(c->members, (u_map_cb_t*)m_who_chan_cb, u);
	} else {
		if ((tu = u_user_by_nick(name)) == NULL)
			goto end;

		/* TODO: chan field */
		who_reply(u, tu, NULL, NULL);
	}

end:
	u_user_num(u, RPL_ENDOFWHO, name);
	return 0;
}

static int list_entry(u_user *u, u_chan *c)
{
	if ((c->mode & (CMODE_PRIVATE | CMODE_SECRET))
	    && !u_chan_user_find(c, u))
		return 0;
	u_user_num(u, RPL_LIST, c->name, c->members->size, c->topic);
	return 0;
}

static int m_list(u_conn *conn, u_msg *msg)
{
	mowgli_patricia_iteration_state_t state;
	u_user *u = conn->priv;
	u_chan *c;

	if (msg->argc > 0) {
		if (!(c = u_chan_get(msg->argv[0])))
			return u_user_num(u, ERR_NOSUCHCHANNEL, msg->argv[0]);
		u_user_num(u, RPL_LISTSTART);
		list_entry(u, c);
		u_user_num(u, RPL_LISTEND);
		return 0;
	}

	u_user_num(u, RPL_LISTSTART);
	MOWGLI_PATRICIA_FOREACH(c, &state, all_chans) {
		if (c->members->size < 3)
			continue;
		list_entry(u, c);
	}
	u_user_num(u, RPL_LISTEND);

	return 0;
}

static void stats_o_cb(u_map *map, char *k, u_oper *o, u_user *u)
{
	char *auth = o->authname[0] ? o->authname : "<any>";
	u_user_num(u, RPL_STATSOLINE, o->name, o->pass, auth);
}

static void stats_i_cb(u_map *map, char *k, u_auth *v, u_user *u)
{
	char buf[CIDR_ADDRSTRLEN];
	u_cidr_to_str(&v->cidr, buf);
	u_user_num(u, RPL_STATSILINE, v->name, v->classname, buf);
}

static int m_stats(u_conn *conn, u_msg *msg)
{
	u_user *u = conn->priv;
	int c, days, hr, min, sec;

	if (!(c = msg->argv[0][0])) /* "STATS :" will do this */
		return u_user_num(u, ERR_NEEDMOREPARAMS, "STATS");

	if (strchr("oi", c) && !(u->flags & UMODE_OPER)) {
		u_user_num(u, ERR_NOPRIVILEGES);
		u_user_num(u, RPL_ENDOFSTATS, c);
		return 0;
	}

	switch (c) {
	case 'o':
		u_map_each(all_opers, (u_map_cb_t*)stats_o_cb, u);
		break;
	case 'i':
		u_map_each(all_auths, (u_map_cb_t*)stats_i_cb, u);
		break;

	case 'u':
		sec = NOW.tv_sec - started;
		min = sec / 60; sec %= 60;
		hr = min / 60; min %= 60;
		days = hr / 24; hr %= 24;

		u_user_num(u, RPL_STATSUPTIME, days, hr, min, sec);
		break;
	}

	u_user_num(u, RPL_ENDOFSTATS, c);
	return 0;
}

static int m_mkpass(u_conn *conn, u_msg *msg)
{
	char buf[CRYPTLEN], salt[CRYPTLEN];

	u_crypto_gen_salt(salt);
	u_crypto_hash(buf, msg->argv[0], salt);

	u_conn_f(conn, ":%S NOTICE %U :%s", &me, conn->priv, buf);
	return 0;
}

static int m_kill(u_conn *conn, u_msg *msg)
{
	u_user *tu, *u = conn->priv;
	char *reason = msg->argv[1] ? msg->argv[1] : "<No reason given>";
	char buf[512];

	if (!(u->flags & UMODE_OPER))
		return u_user_num(u, ERR_NOPRIVILEGES);
	if (!(tu = u_user_by_nick(msg->argv[0])))
		return u_user_num(u, ERR_NOSUCHNICK, msg->argv[0]);

	snf(FMT_USER, buf, 512, "%U (%s)", u, reason);

	u_sendto_visible(tu, ST_USERS, ":%H QUIT :Killed (%s)", tu, buf);
	u_roster_f(R_SERVERS, NULL, ":%U KILL %U :%s", u, tu, me.name, buf);

	if (IS_LOCAL_USER(tu))
		u_conn_f(USER_LOCAL(tu)->conn, ":%H QUIT :Killed (%s)", tu, buf);
	u_user_unlink(tu);

	return 0;
}

static int m_kick(u_conn *conn, u_msg *msg)
{
	u_user *tu, *u = conn->priv;
	u_chan *c;
	u_chanuser *tcu, *cu;
	char *r = msg->argv[2];

	if (!(c = u_chan_get(msg->argv[0])))
		return u_user_num(u, ERR_NOSUCHCHANNEL, msg->argv[0]);
	if (!(tu = u_user_by_nick(msg->argv[1])))
		return u_user_num(u, ERR_NOSUCHNICK, msg->argv[1]);
	if (!(cu = u_chan_user_find(c, u)))
		return u_user_num(u, ERR_NOTONCHANNEL, c);
	if (!(tcu = u_chan_user_find(c, tu)))
		return u_user_num(u, ERR_USERNOTINCHANNEL, tu, c);
	if (!(cu->flags & CU_PFX_OP))
		return u_user_num(u, ERR_CHANOPRIVSNEEDED, c);

	u_log(LG_FINE, "%U KICK %U from %C (reason=%s)", u, tu, c, r);
	r = r ? r : tu->nick;
	u_sendto_chan(c, NULL, ST_USERS, ":%H KICK %C %U :%s", u, c, tu, r);
	u_roster_f(R_SERVERS, NULL, ":%H KICK %C %U :%s", u, c, tu, r);

	u_chan_user_del(tcu);

	return 0;
}

static int m_summon(u_conn *conn, u_msg *msg)
{
	u_conn_num(conn, ERR_SUMMONDISABLED);
	return 0;
}

static int m_invite(u_conn *conn, u_msg *msg)
{
	u_entity te;
	u_user *tu, *u = conn->priv;
	u_chan *c;
	u_chanuser *cu;

	if (!(tu = u_user_by_nick(msg->argv[0])))
		return u_user_num(u, ERR_NOSUCHNICK, msg->argv[0]);
	if (!(c = u_chan_get(msg->argv[1])))
		return u_user_num(u, ERR_NOSUCHCHANNEL, msg->argv[1]);
	if (!(cu = u_chan_user_find(c, u)))
		return u_user_num(u, ERR_NOTONCHANNEL, c);
	if (u_chan_user_find(c, tu))
		return u_user_num(u, ERR_USERONCHANNEL, tu, c);
	if (!(cu->flags & CU_PFX_OP) && !(c->mode & CMODE_FREEINVITE))
		return u_user_num(u, ERR_CHANOPRIVSNEEDED, c);

	u_entity_from_user(&te, tu);
	if (ENT_IS_LOCAL(&te)) {
		u_add_invite(c, tu);
		u_conn_f(te.link, ":%H INVITE %U :%C", u, tu, c);
	} else {
		u_conn_f(te.link, ":%H INVITE %U %C :%u", u, tu, c, c->ts);
	}

	/* TODO: who sees RPL_INVITING? */
	u_user_num(u, RPL_INVITING, tu, c);
	u_log(LG_VERBOSE, "Local %U invited %U to %C", u, tu, c);

	return 0;
}

static int m_modload(u_conn *conn, u_msg *msg)
{
	u_user *u = conn->priv;

	if (!(u->flags & UMODE_OPER))
		return u_user_num(u, ERR_NOPRIVILEGES);

	u_module_load(msg->argv[0]);

	return 0;
}

static int m_modunload(u_conn *conn, u_msg *msg)
{
	u_user *u = conn->priv;

	if (!(u->flags & UMODE_OPER))
		return u_user_num(u, ERR_NOPRIVILEGES);

	u_module_unload(msg->argv[0]);

	return 0;
}

static int m_modreload(u_conn *conn, u_msg *msg)
{
	u_user *u = conn->priv;

	if (!(u->flags & UMODE_OPER))
		return u_user_num(u, ERR_NOPRIVILEGES);

	u_module_reload_or_load(msg->argv[0]);

	return 0;
}

static int m_modlist(u_conn *conn, u_msg *msg)
{
	mowgli_patricia_iteration_state_t state;
	u_module *mod;
	u_user *u = conn->priv;

	if (!(u->flags & UMODE_OPER))
		return u_user_num(u, ERR_NOPRIVILEGES);

	MOWGLI_PATRICIA_FOREACH(mod, &state, u_modules) {
		u_conn_f(conn, ":%S NOTICE %G :%s: %s", &me, conn,
		        mod->info->name, mod->info->description);
	}

	return 0;
}

u_cmd c_user[] = {
	{ "ECHO",      CTX_USER, m_echo,              0, 0 },
	{ "QUIT",      CTX_USER, m_quit,              0, 0 },
	{ "VERSION",   CTX_USER, m_version,           0, 0 },
	{ "MOTD",      CTX_USER, m_motd,              0, 0 },
	{ "JOIN",      CTX_USER, m_join,              1, 0 },
	{ "PART",      CTX_USER, m_part,              1, 0 },
	{ "TOPIC",     CTX_USER, m_topic,             1, 0 },
	{ "NAMES",     CTX_USER, m_names,             0, 0 },
	{ "MODE",      CTX_USER, m_mode,              1, 0 },
	{ "WHOIS",     CTX_USER, m_whois,             1, 0 },
	{ "USERHOST",  CTX_USER, m_userhost,          1, 0 },
	{ "WHO",       CTX_USER, m_who,               1, 0 },
	{ "OPER",      CTX_USER, m_oper,              2, 0 },
	{ "LIST",      CTX_USER, m_list,              0, 0 },
	{ "NICK",      CTX_USER, m_nick,              1, 0 },
	{ "STATS",     CTX_USER, m_stats,             1, 0 },
	{ "MKPASS",    CTX_USER, m_mkpass,            1, 0 },
	{ "ADMIN",     CTX_USER, m_admin,             0, 0 },
	{ "KILL",      CTX_USER, m_kill,              1, 0 },
	{ "KICK",      CTX_USER, m_kick,              2, 0 },
	{ "SUMMON",    CTX_USER, m_summon,            0, 0 },
	{ "MAP",       CTX_USER, m_map,               0, 0 },
	{ "INVITE",    CTX_USER, m_invite,            2, 0 },

	{ "MODLOAD",   CTX_USER, m_modload,           1, 0 },
	{ "MODUNLOAD", CTX_USER, m_modunload,         1, 0 },
	{ "MODRELOAD", CTX_USER, m_modreload,         1, 0 },
	{ "MODLIST",   CTX_USER, m_modlist,           0, 0 },

	{ "SQUIT",     CTX_USER, not_implemented,     0, 0 },
	{ "LINKS",     CTX_USER, not_implemented,     0, 0 },
	{ "TIME",      CTX_USER, not_implemented,     0, 0 },
	{ "CONNECT",   CTX_USER, not_implemented,     0, 0 },
	{ "TRACE",     CTX_USER, not_implemented,     0, 0 },
	{ "INFO",      CTX_USER, not_implemented,     0, 0 },
	{ "WHOWAS",    CTX_USER, not_implemented,     0, 0 },
	{ "REHASH",    CTX_USER, not_implemented,     0, 0 },
	{ "RESTART",   CTX_USER, not_implemented,     0, 0 },
	{ "USERS",     CTX_USER, not_implemented,     0, 0 },
	{ "OPERWALL",  CTX_USER, not_implemented,     0, 0 },
	{ "ISON",      CTX_USER, not_implemented,     0, 0 },

	{ "" },
};
