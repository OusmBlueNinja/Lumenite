local db = require("lumenite.db")

-- Define a User model
local User = db.Model({
    __tablename = "users",
    id = db.Column("id", db.Integer, { primary_key = true }),
    name = db.Column("name", db.String),
    created_at = db.Column("created_at", db.String)
})

db.create_all()

local user = User({
    id = 1,
    name = "Dave",
    created_at = "2024-01-01"
})

db.session.add(user)
db.session.commit()


-- Query: get latest 2 users
local results = User.query.order_by(User.created_at.desc()).limit(2).all()

-- db.session.delete(results)
-- db.session.commit()

-- Print results
for i, user in ipairs(results) do
    print(string.format("[%d] %s (%s)", user.id, user.name, user.created_at))
end
