"""Dated notes shared between a bound desk and its family."""
import base64
import binascii
from datetime import date

from fastapi import APIRouter, Cookie, Header, HTTPException, Request, Response
from pydantic import BaseModel, Field

from api.shared import ensure_web_or_device_access
from core.auth import validate_mac_param
from core.db import get_main_db

router = APIRouter(prefix="/desk-notes")
SIZE = 736 * 288 // 8


class Note(BaseModel):
    text: str = Field(max_length=500)
    pixels: str = Field(max_length=SIZE * 3)


async def database():
    db = await get_main_db()
    await db.execute("""CREATE TABLE IF NOT EXISTS desk_notes (
        mac TEXT NOT NULL, day TEXT NOT NULL, text TEXT NOT NULL, frame BLOB NOT NULL,
        PRIMARY KEY(mac, day))""")
    return db


@router.put("/{mac}/{day}")
async def save(mac: str, day: date, note: Note, request: Request, ink_session: str | None = Cookie(None)):
    mac = validate_mac_param(mac)
    await ensure_web_or_device_access(request, mac, None, ink_session, allow_device_token=False)
    try:
        packed = base64.b64decode(note.pixels, validate=True)
    except (ValueError, binascii.Error):
        raise HTTPException(422, "Invalid image")
    if len(packed) % 2 or 0 in packed[::2] or sum(packed[::2]) != SIZE:
        raise HTTPException(422, "Invalid image size")
    frame = b"".join(bytes([v]) * n for n, v in zip(packed[::2], packed[1::2]))
    db = await database()
    await db.execute("INSERT OR REPLACE INTO desk_notes VALUES(?,?,?,?)",
                     (mac, day.isoformat(), note.text.strip(), frame))
    await db.commit()
    return {"ok": True}


@router.get("/{mac}/{day}")
async def read(mac: str, day: date, request: Request, device: bool = False,
               ink_session: str | None = Cookie(None), x_device_token: str | None = Header(None)):
    mac = validate_mac_param(mac)
    await ensure_web_or_device_access(request, mac, x_device_token, ink_session)
    db = await database()
    async with db.execute("SELECT text,frame FROM desk_notes WHERE mac=? AND day=?", (mac, day.isoformat())) as cur:
        row = await cur.fetchone()
    headers = {"Cache-Control": "private, no-store"}
    if device:
        if row is None:
            return Response(status_code=204, headers=headers)
        return Response(row[1] + row[0].encode(), media_type="application/octet-stream", headers=headers)
    return Response(row[0] if row else "", media_type="text/plain", headers=headers)
